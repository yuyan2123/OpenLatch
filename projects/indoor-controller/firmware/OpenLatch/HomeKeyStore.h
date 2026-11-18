#pragma once
#include <nvs.h>
#include <atomic>
#include <algorithm>
#include <cstring>
#include <mutex>
#include "ddk/store/CredentialStore.h"
#include "ddk/store/ReaderIdentity.h"
#include "ddk/store/Issuer.h"

// The owner holds mutex for each complete authentication or HAP transaction.
class HomeKeyStore final : public ddk::CredentialStore {
 public:
  std::mutex mutex;
  bool begin() {
    healthy_ = false;
    opened_ = nvs_open("door_homekey", NVS_READWRITE, &handle_) == ESP_OK;
    if (!opened_) return false;
    std::vector<uint8_t> record(kMaxRecord);
    size_t size = record.size();
    const auto result = nvs_get_blob(handle_, "credentials", record.data(), &size);
    if (result == ESP_ERR_NVS_NOT_FOUND) { healthy_ = true; return true; }
    if (result != ESP_OK || size > kMaxRecord || size < 8) return false;
    record.resize(size);
    if (std::memcmp(record.data(), "HK01", 4) || get32(record.data() + size - 4) != crc(record.data(), size - 4)) return false;
    ddk::ReaderIdentity identity;
    std::vector<ddk::Issuer> issuers;
    Cursor input{record.data() + 4, size - 8};
    identity.private_key = input.bytes(32); identity.public_key = input.bytes(65);
    identity.public_key_x = input.bytes(32); identity.group_identifier = input.bytes(8);
    identity.sub_identifier = input.bytes(8);
    const uint8_t count = input.byte();
    if (count > 16) return false;
    for (uint8_t i = 0; i < count; ++i) {
      ddk::Issuer issuer;
      issuer.id = input.bytes(8); issuer.public_key = input.bytes(32);
      const uint8_t endpoints = input.byte();
      if (endpoints > 32) return false;
      for (uint8_t j = 0; j < endpoints; ++j) {
        ddk::Endpoint endpoint;
        endpoint.id = input.bytes(6); endpoint.public_key = input.bytes(65);
        endpoint.public_key_x = input.bytes(32); endpoint.persistent_key = input.bytes(32);
        endpoint.counter = input.byte();
        issuer.endpoints.push_back(std::move(endpoint));
      }
      issuers.push_back(std::move(issuer));
    }
    if (!input.ok || input.remaining || !valid(identity, issuers)) return false;
    identity_ = std::move(identity); issuers_ = std::move(issuers); healthy_ = true;
    return true;
  }
  const ddk::ReaderIdentity& reader_identity() const override { return identity_; }
  void provision_identity(const ddk::ReaderIdentity& identity) override {
    if (identity_.private_key.empty()) identity_ = identity;
  }
  ddk::span<ddk::Issuer> issuers() override { return issuers_; }
  std::vector<ddk::Issuer>& issuerList() { return issuers_; }
  void clearIdentity() { identity_ = {}; save(); }
  void clearAll() { identity_ = {}; issuers_.clear(); save(); }
  bool healthy() const { return healthy_.load(); }
  bool provisioned() const { return healthy_ && identity_.private_key.size() == 32 && !issuers_.empty(); }
  uint32_t generation() const { return generation_.load(); }
  void save() override {
    healthy_ = false;
    if (!opened_ || !valid(identity_, issuers_)) return;
    std::vector<uint8_t> record{'H','K','0','1'};
    const auto bytes = [&record](const std::vector<uint8_t>& value) {
      record.push_back(uint8_t(value.size())); record.insert(record.end(), value.begin(), value.end());
    };
    bytes(identity_.private_key); bytes(identity_.public_key); bytes(identity_.public_key_x);
    bytes(identity_.group_identifier); bytes(identity_.sub_identifier);
    record.push_back(uint8_t(issuers_.size()));
    for (const auto& issuer : issuers_) {
      bytes(issuer.id); bytes(issuer.public_key); record.push_back(uint8_t(issuer.endpoints.size()));
      for (const auto& endpoint : issuer.endpoints) {
        bytes(endpoint.id); bytes(endpoint.public_key); bytes(endpoint.public_key_x); bytes(endpoint.persistent_key);
        record.push_back(endpoint.counter);
      }
    }
    const uint32_t checksum = crc(record.data(), record.size());
    for (int i = 0; i < 4; ++i) record.push_back(uint8_t(checksum >> (8 * i)));
    if (record.size() > kMaxRecord || nvs_set_blob(handle_, "credentials", record.data(), record.size()) != ESP_OK ||
        nvs_commit(handle_) != ESP_OK) return;
    std::vector<uint8_t> verify(record.size()); size_t size = verify.size();
    if (nvs_get_blob(handle_, "credentials", verify.data(), &size) != ESP_OK || size != record.size() || verify != record) return;
    ++generation_; healthy_ = true;
  }
 private:
  struct Cursor {
    const uint8_t* data; size_t remaining; bool ok = true;
    uint8_t byte() { if (!remaining) { ok = false; return 0; } --remaining; return *data++; }
    std::vector<uint8_t> bytes(size_t maximum) {
      const size_t size = byte();
      if (!ok || size > maximum || size > remaining) { ok = false; return {}; }
      std::vector<uint8_t> value(data, data + size); data += size; remaining -= size; return value;
    }
  };
  static bool valid(const ddk::ReaderIdentity& identity, const std::vector<ddk::Issuer>& issuers) {
    const bool empty = identity.private_key.empty() && identity.public_key.empty() && identity.public_key_x.empty() &&
                       identity.group_identifier.empty() && identity.sub_identifier.empty();
    if (!empty && (identity.private_key.size() != 32 || identity.public_key.size() != 65 || identity.public_key[0] != 4 ||
        identity.public_key_x.size() != 32 || identity.group_identifier.size() != 8 || identity.sub_identifier.size() != 8)) return false;
    if (issuers.size() > 16) return false;
    size_t endpoints = 0;
    for (size_t i = 0; i < issuers.size(); ++i) {
      const auto& issuer = issuers[i];
      if (issuer.id.size() != 8 || issuer.public_key.size() != 32) return false;
      for (size_t j = 0; j < i; ++j) if (issuers[j].id == issuer.id) return false;
      endpoints += issuer.endpoints.size(); if (endpoints > 32) return false;
      for (size_t j = 0; j < issuer.endpoints.size(); ++j) {
        const auto& endpoint = issuer.endpoints[j];
        if (endpoint.id.size() != 6 || endpoint.public_key.size() != 65 || endpoint.public_key[0] != 4 ||
            endpoint.public_key_x.size() != 32 || (!endpoint.persistent_key.empty() && endpoint.persistent_key.size() != 32)) return false;
        for (size_t k = 0; k < j; ++k) if (issuer.endpoints[k].id == endpoint.id) return false;
      }
    }
    return true;
  }
  static uint32_t get32(const uint8_t* data) {
    uint32_t value = 0; for (int i = 0; i < 4; ++i) value |= uint32_t(data[i]) << (8 * i); return value;
  }
  static uint32_t crc(const uint8_t* data, size_t size) {
    uint32_t value = 0xffffffffu;
    for (size_t i = 0; i < size; ++i) {
      value ^= data[i];
      for (int bit = 0; bit < 8; ++bit) value = (value >> 1) ^ ((value & 1) ? 0xedb88320u : 0u);
    }
    return ~value;
  }
  static constexpr size_t kMaxRecord = 8192;
  ddk::ReaderIdentity identity_;
  std::vector<ddk::Issuer> issuers_;
  std::atomic<bool> healthy_{false};
  std::atomic<uint32_t> generation_{0};
  nvs_handle_t handle_ = 0;
  bool opened_ = false;
};
