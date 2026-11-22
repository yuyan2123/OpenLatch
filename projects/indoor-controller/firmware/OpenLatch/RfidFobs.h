#pragma once
#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <nvs.h>
#include "CredentialNames.h"

namespace door {
struct RfidUid {
  std::array<uint8_t, 10> bytes{};
  uint8_t length = 0;
  bool valid() const {
    return (length == 4 || length == 7 || length == 10) &&
           !(length == 4 && bytes[0] == 0x08) &&
           std::any_of(bytes.begin(), bytes.begin() + length, [](uint8_t byte) { return byte != 0; }) &&
           std::all_of(bytes.begin() + length, bytes.end(), [](uint8_t byte) { return byte == 0; });
  }
  bool operator==(const RfidUid& other) const { return length == other.length && bytes == other.bytes; }
};

// The main loop owns the list and learning state. The reader already enforces
// a 500 ms removal interval between taps, including retries and reconnections.
class RfidFobs {
 public:
  static constexpr uint8_t kCapacity = 16;
  enum class Event { Ignored, Captured, Mismatch, Learned, Duplicate, Matched, StorageError, TimedOut };

  bool begin() {
    opened_ = nvs_open("door_rfid", NVS_READWRITE, &handle_) == ESP_OK;
    ready_ = opened_ && load();
    return ready_;
  }
  bool ready() const { return ready_; }
  bool learning() const { return learning_; }
  uint32_t revision() const { return revision_; }
  bool has(uint8_t slot) const { return slot < kCapacity && entries_[slot].length != 0; }
  uint8_t count() const {
    return std::count_if(entries_.begin(), entries_.end(), [](const RfidUid& uid) { return uid.length != 0; });
  }
  CredentialName displayName(uint8_t slot) const {
    return has(slot) ? CredentialNames::load(handle_, slot, identity(entries_[slot])) : CredentialName{};
  }
  bool rename(uint8_t slot, const CredentialName& label) {
    return ready_ && !learning_ && has(slot) && CredentialNames::save(handle_, slot, identity(entries_[slot]), label);
  }
  bool learn(uint32_t now) {
    if (!ready_ || learning_ || count() >= kCapacity) return false;
    startedAt_ = now;
    learning_ = true;
    ++revision_;
    return true;
  }
  void cancel() {
    if (!learning_) return;
    learning_ = false;
    candidate_ = {};
    ++revision_;
  }
  bool tick(uint32_t now) {
    if (!learning_ || uint32_t(now - startedAt_) < 30000) return false;
    cancel();
    return true;
  }
  Event receive(uint32_t now, const RfidUid& uid) {
    if (tick(now)) return Event::TimedOut;
    if (!ready_ || !uid.valid()) return Event::Ignored;
    const bool known = std::find(entries_.begin(), entries_.end(), uid) != entries_.end();
    if (!learning_) return known ? Event::Matched : Event::Ignored;
    if (!candidate_.length) { candidate_ = uid; return Event::Captured; }
    if (!(candidate_ == uid)) return Event::Mismatch;
    if (known) { cancel(); return Event::Duplicate; }
    auto next = entries_;
    *std::find(next.begin(), next.end(), RfidUid{}) = uid;
    return persist(next) ? Event::Learned : Event::StorageError;
  }
  bool remove(uint8_t slot) {
    if (!ready_ || learning_ || !has(slot)) return false;
    auto next = entries_;
    next[slot] = {};
    return persist(next);
  }
  bool clear() { return !learning_ && persist({}); }

 private:
  using Entries = std::array<RfidUid, kCapacity>;
  static constexpr uint8_t kHeader[] = {'R', 'F', 'I', 1};
  // NVS checks blob integrity. Encode explicitly to keep the layout independent of C++ padding.
  using Record = std::array<uint8_t, sizeof(kHeader) + kCapacity * 11>;

  static CredentialNames::Identity identity(const RfidUid& uid) {
    CredentialNames::Identity id{uid.length};
    std::copy(uid.bytes.begin(), uid.bytes.end(), id.begin() + 1);
    return id;
  }

  bool load() {
    entries_ = {};
    Record record{};
    size_t size = record.size();
    const auto result = nvs_get_blob(handle_, "fobs", record.data(), &size);
    if (result == ESP_ERR_NVS_NOT_FOUND) return true;
    if (result != ESP_OK || size != record.size() ||
        !std::equal(std::begin(kHeader), std::end(kHeader), record.begin())) return false;
    Entries decoded{};
    auto input = record.begin() + sizeof(kHeader);
    for (auto entry = decoded.begin(); entry != decoded.end(); ++entry) {
      entry->length = *input++;
      std::copy_n(input, entry->bytes.size(), entry->bytes.begin());
      input += entry->bytes.size();
      if (*entry == RfidUid{}) continue;
      if (!entry->valid() || std::find(decoded.begin(), entry, *entry) != entry) return false;
    }
    entries_ = decoded;
    return true;
  }
  bool persist(const Entries& next) {
    if (learning_) cancel();
    else ++revision_;
    Record record{}, verified{};
    auto output = std::copy(std::begin(kHeader), std::end(kHeader), record.begin());
    for (const auto& uid : next) {
      *output++ = uid.length;
      output = std::copy(uid.bytes.begin(), uid.bytes.end(), output);
    }
    size_t size = verified.size();
    ready_ = opened_ && nvs_set_blob(handle_, "fobs", record.data(), record.size()) == ESP_OK &&
             nvs_commit(handle_) == ESP_OK && nvs_get_blob(handle_, "fobs", verified.data(), &size) == ESP_OK &&
             size == record.size() && verified == record;
    if (ready_) {
      for (uint8_t slot = 0; slot < kCapacity; ++slot)
        if (!(entries_[slot] == next[slot])) (void)CredentialNames::clear(handle_, slot);
      entries_ = next;
    } else entries_ = {};
    return ready_;
  }
  nvs_handle_t handle_ = 0;
  Entries entries_{};
  RfidUid candidate_{};
  uint32_t startedAt_ = 0, revision_ = 0;
  bool opened_ = false, ready_ = false, learning_ = false;
};
}  // namespace door
