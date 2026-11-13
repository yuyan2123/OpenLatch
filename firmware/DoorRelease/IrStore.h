#pragma once
#include <cstddef>
#include <cstring>
#include <nvs.h>
#include "Infrared.h"

// One versioned, checksummed NVS blob. A tombstone disables the stored key.
// nvs_set_blob + nvs_commit avoids partially updated protocol/address fields.
class IrStore final : public door::IrStorage {
 public:
  door::IrLoad load(door::IrKey& key) override {
    key = {};
    if (!opened_) {
      opened_ = nvs_open("door_ir", NVS_READWRITE, &handle_) == ESP_OK;
      if (!opened_) return door::IrLoad::Error;
    }
    uint8_t record[kSize]{};
    size_t length = sizeof(record);
    const auto result = nvs_get_blob(handle_, "key", record, &length);
    if (result == ESP_ERR_NVS_NOT_FOUND) return door::IrLoad::Empty;
    if (result != ESP_OK || length != kSize || !decode(record, key)) return door::IrLoad::Error;
    return key.valid() ? door::IrLoad::Loaded : door::IrLoad::Empty;
  }
  bool save(const door::IrKey& key) override {
    if (!opened_) return false;
    uint8_t record[kSize]{};
    record[0] = 'I'; record[1] = 'R'; record[2] = 1;
    record[3] = key.valid() ? 1 : 0;
    put16(record + 4, uint16_t(key.protocol));
    put16(record + 6, key.address);
    put16(record + 8, key.command);
    put16(record + 10, key.bits);
    const uint32_t crc = checksum(record, 12);
    for (int i = 0; i < 4; ++i) record[12 + i] = uint8_t(crc >> (8 * i));
    if (nvs_set_blob(handle_, "key", record, kSize) != ESP_OK || nvs_commit(handle_) != ESP_OK)
      return false;
    uint8_t verify[kSize]{};
    size_t length = sizeof(verify);
    return nvs_get_blob(handle_, "key", verify, &length) == ESP_OK && length == kSize &&
           std::memcmp(record, verify, kSize) == 0;
  }

 private:
  static constexpr size_t kSize = 16;
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
  static bool decode(const uint8_t* record, door::IrKey& key) {
    if (record[0] != 'I' || record[1] != 'R' || record[2] != 1 || record[3] > 1) return false;
    const uint32_t crc = checksum(record, 12);
    for (int i = 0; i < 4; ++i)
      if (record[12 + i] != uint8_t(crc >> (8 * i))) return false;
    key = {static_cast<door::IrProtocol>(get16(record + 4)), get16(record + 6),
           get16(record + 8), get16(record + 10)};
    if (record[3]) return key.valid();
    return key == door::IrKey{};
  }
  nvs_handle_t handle_ = 0;
  bool opened_ = false;
};
