#pragma once
#include <cstddef>
#include <cstring>
#include <nvs.h>
#include "Infrared.h"

// One versioned, checksummed NVS blob. Version 1's single key is read without
// rewriting flash; the next mutation upgrades it atomically to a v2 list.
// nvs_set_blob + nvs_commit avoids partially updated protocol/address fields.
class IrStore final : public door::IrStorage {
 public:
  explicit IrStore(const char* storageNamespace = "door_ir") : namespace_(storageNamespace) {}
  door::IrLoad load(door::IrKeys& keys) override {
    keys.clear();
    if (!opened_) {
      opened_ = nvs_open(namespace_, NVS_READWRITE, &handle_) == ESP_OK;
      if (!opened_) return door::IrLoad::Error;
    }
    uint8_t record[kSize]{};
    size_t length = sizeof(record);
    const auto result = nvs_get_blob(handle_, "key", record, &length);
    if (result == ESP_ERR_NVS_NOT_FOUND) return door::IrLoad::Empty;
    if (result != ESP_OK || !decode(record, length, keys)) return door::IrLoad::Error;
    return keys.count() ? door::IrLoad::Loaded : door::IrLoad::Empty;
  }
  bool save(const door::IrKeys& keys) override {
    if (!opened_ || !keys.valid()) return false;
    uint8_t record[kSize]{};
    record[0] = 'I'; record[1] = 'R'; record[2] = 2;
    record[3] = keys.count();
    for (uint8_t slot = 0; slot < door::kIrMaxKeys; ++slot) {
      const auto& key = keys.entries[slot];
      auto* entry = record + 4 + slot * 8;
      put16(entry, uint16_t(key.protocol)); put16(entry + 2, key.address);
      put16(entry + 4, key.command); put16(entry + 6, key.bits);
    }
    const uint32_t crc = checksum(record, kSize - 4);
    for (int i = 0; i < 4; ++i) record[kSize - 4 + i] = uint8_t(crc >> (8 * i));
    if (nvs_set_blob(handle_, "key", record, kSize) != ESP_OK || nvs_commit(handle_) != ESP_OK)
      return false;
    uint8_t verify[kSize]{};
    size_t length = sizeof(verify);
    return nvs_get_blob(handle_, "key", verify, &length) == ESP_OK && length == kSize &&
           std::memcmp(record, verify, kSize) == 0;
  }

 private:
  static constexpr size_t kSize = 8 + door::kIrMaxKeys * 8;
  static void put16(uint8_t* out, uint16_t value) {
    out[0] = uint8_t(value); out[1] = uint8_t(value >> 8);
  }
  static uint16_t get16(const uint8_t* in) { return uint16_t(in[0]) | (uint16_t(in[1]) << 8); }
  static uint32_t checksum(const uint8_t* data, size_t length) {
    uint32_t crc = 0xffffffffu;
    for (size_t i = 0; i < length; ++i) {
      crc ^= data[i];
      for (int bit = 0; bit < 8; ++bit)
        crc = (crc >> 1) ^ ((crc & 1) ? 0xedb88320u : 0u);
    }
    return ~crc;
  }
  static bool decode(const uint8_t* record, size_t length, door::IrKeys& keys) {
    if (length != 16 && length != kSize) return false;
    if (record[0] != 'I' || record[1] != 'R') return false;
    const bool legacy = record[2] == 1 && length == 16 && record[3] <= 1;
    if (!legacy && (record[2] != 2 || length != kSize || record[3] > door::kIrMaxKeys)) return false;
    const uint32_t crc = checksum(record, length - 4);
    for (int i = 0; i < 4; ++i)
      if (record[length - 4 + i] != uint8_t(crc >> (8 * i))) return false;
    for (uint8_t slot = 0; slot < (legacy ? 1 : door::kIrMaxKeys); ++slot) {
      const auto* entry = record + 4 + slot * 8;
      keys.entries[slot] = {static_cast<door::IrProtocol>(get16(entry)), get16(entry + 2),
                            get16(entry + 4), get16(entry + 6)};
    }
    return keys.valid() && keys.count() == record[3];
  }
  nvs_handle_t handle_ = 0;
  const char* namespace_;
  bool opened_ = false;
};
