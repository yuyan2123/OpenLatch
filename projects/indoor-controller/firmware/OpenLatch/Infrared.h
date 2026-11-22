#pragma once
#include <cstdint>
#include "CredentialName.h"

namespace door {
constexpr uint32_t kIrQuietMs = 500;
constexpr uint32_t kIrLearnMs = 30000;
constexpr uint8_t kIrMaxKeys = 16;

// Stable storage IDs, independent of the IRremote library's enum ordering.
enum class IrProtocol : uint16_t { None, Nec, Sony, Samsung, Rc5, Rc6, Jvc };
inline const char* name(IrProtocol protocol) {
  switch (protocol) {
    case IrProtocol::None: return "none";
    case IrProtocol::Nec: return "NEC";
    case IrProtocol::Sony: return "Sony";
    case IrProtocol::Samsung: return "Samsung";
    case IrProtocol::Rc5: return "RC5";
    case IrProtocol::Rc6: return "RC6";
    case IrProtocol::Jvc: return "JVC";
  }
  return "invalid";
}
struct IrKey {
  IrProtocol protocol = IrProtocol::None;
  uint16_t address = 0, command = 0, bits = 0;
  bool valid() const {
    return protocol >= IrProtocol::Nec && protocol <= IrProtocol::Jvc && bits > 0 && bits <= 64;
  }
  bool operator==(const IrKey& other) const {
    return protocol == other.protocol && address == other.address &&
           command == other.command && bits == other.bits;
  }
};
enum class IrLoad { Empty, Loaded, Error };
struct IrKeys {
  IrKey entries[kIrMaxKeys]{};
  void clear() { for (auto& key : entries) key = IrKey{}; }
  uint8_t count() const {
    uint8_t result = 0;
    for (const auto& key : entries) if (key.valid()) ++result;
    return result;
  }
  bool contains(const IrKey& input) const {
    if (!input.valid()) return false;
    for (const auto& key : entries) if (key == input) return true;
    return false;
  }
  bool valid() const {
    for (uint8_t i = 0; i < kIrMaxKeys; ++i) {
      if (!entries[i].valid() && !(entries[i] == IrKey{})) return false;
      if (entries[i].valid())
        for (uint8_t j = 0; j < i; ++j) if (entries[i] == entries[j]) return false;
    }
    return true;
  }
};
struct IrStorage {
  virtual ~IrStorage() = default;
  virtual IrLoad load(IrKeys& keys) = 0;
  // Save the whole list atomically; empty slots must be all zero.
  virtual bool save(const IrKeys& keys) = 0;
  virtual CredentialName displayName(uint8_t, const IrKey&) const { return {}; }
  virtual bool rename(uint8_t, const IrKey&, const CredentialName&) { return false; }
  virtual void clearName(uint8_t) {}
};
enum class IrEvent { Ignored, Captured, Mismatch, Learned, Duplicate, Matched, StorageError, TimedOut };
inline const char* name(IrEvent event) {
  switch (event) {
    case IrEvent::Ignored: return "ignored";
    case IrEvent::Captured: return "captured";
    case IrEvent::Mismatch: return "mismatch";
    case IrEvent::Learned: return "learned";
    case IrEvent::Duplicate: return "already_saved";
    case IrEvent::Matched: return "matched";
    case IrEvent::StorageError: return "storage_error";
    case IrEvent::TimedOut: return "timed_out";
  }
  return "invalid";
}

class Infrared {
 public:
  explicit Infrared(IrStorage& storage) : storage_(storage) {}
  bool begin(uint32_t now) {
    keys_.clear(); candidate_ = {};
    learning_ = false;
    lastActivity_ = now;
    const auto result = storage_.load(keys_);
    ready_ = result != IrLoad::Error && keys_.valid() &&
             ((result == IrLoad::Loaded) == (keys_.count() > 0));
    if (!ready_) keys_.clear();
    return ready_;
  }
  bool learn(uint32_t now) {
    if (!ready_ || count() >= kIrMaxKeys) return false;
    learning_ = true;
    candidate_ = {};
    startedAt_ = lastActivity_ = now;
    return true;
  }
  void cancel(uint32_t now) {
    learning_ = false;
    candidate_ = {};
    lastActivity_ = now;
  }
  bool forget(uint32_t now) {
    // An empty verified list also repairs corrupt storage in this namespace.
    const IrKeys empty;
    return persist(now, empty);
  }
  bool remove(uint32_t now, uint8_t slot) {
    if (!ready_ || learning_ || slot >= kIrMaxKeys || !keys_.entries[slot].valid()) return false;
    IrKeys next = keys_;
    next.entries[slot] = {};
    return persist(now, next);
  }
  bool tick(uint32_t now) {
    if (!learning_ || uint32_t(now - startedAt_) < kIrLearnMs) return false;
    cancel(now);
    return true;
  }
  // Every decoded frame, including repeats/noise, updates the quiet interval.
  // A held button cannot rearm even if its protocol sends full repeat frames.
  IrEvent receive(uint32_t now, const IrKey& input, bool repeat = false, bool invalid = false) {
    const bool expired = tick(now);
    const bool quiet = uint32_t(now - lastActivity_) >= kIrQuietMs;
    lastActivity_ = now;
    if (expired) return IrEvent::TimedOut;
    if (!ready_ || invalid || repeat || !quiet || !input.valid()) return IrEvent::Ignored;
    if (learning_) {
      if (!candidate_.valid()) {
        candidate_ = input;
        return IrEvent::Captured;
      }
      if (!(input == candidate_)) return IrEvent::Mismatch;
      if (keys_.contains(candidate_)) {
        cancel(now);
        return IrEvent::Duplicate;
      }
      IrKeys next = keys_;
      for (auto& key : next.entries) if (!key.valid()) { key = candidate_; break; }
      // Persist before enabling any new key; learning frames never release.
      return persist(now, next) ? IrEvent::Learned : IrEvent::StorageError;
    }
    return enabled() && contains(input) ? IrEvent::Matched : IrEvent::Ignored;
  }
  bool ready() const { return ready_; }
  bool learning() const { return learning_; }
  bool enabled() const { return ready_ && !learning_ && count() > 0; }
  uint8_t count() const { return keys_.count(); }
  bool contains(const IrKey& input) const { return keys_.contains(input); }
  CredentialName displayName(uint8_t slot) const {
    return slot < kIrMaxKeys && at(slot).valid() ? storage_.displayName(slot, at(slot)) : CredentialName{};
  }
  bool rename(uint8_t slot, const CredentialName& label) {
    return ready_ && !learning_ && slot < kIrMaxKeys && at(slot).valid() && storage_.rename(slot, at(slot), label);
  }
  const IrKey& at(uint8_t slot) const { return keys_.entries[slot < kIrMaxKeys ? slot : 0]; }
  // First saved key for legacy status consumers. List APIs expose every slot.
  const IrKey& key() const {
    for (const auto& key : keys_.entries) if (key.valid()) return key;
    return keys_.entries[0];
  }

 private:
  IrStorage& storage_;
  IrKeys keys_{};
  IrKey candidate_{};
  bool persist(uint32_t now, const IrKeys& next) {
    ready_ = storage_.save(next);
    if (ready_) {
      for (uint8_t slot = 0; slot < kIrMaxKeys; ++slot)
        if (!(keys_.entries[slot] == next.entries[slot])) storage_.clearName(slot);
      keys_ = next;
    } else keys_.clear();
    cancel(now);
    return ready_;
  }
  uint32_t startedAt_ = 0, lastActivity_ = 0;
  bool ready_ = false, learning_ = false;
};
}  // namespace door
