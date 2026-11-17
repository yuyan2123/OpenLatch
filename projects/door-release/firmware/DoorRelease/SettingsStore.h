#pragma once
#include <nvs.h>
#include "DeviceSettings.h"

class SettingsStore {
 public:
  enum class Load { Missing, Loaded, Error };
  Load load(door::DeviceSettings& settings) {
    if (!opened_) {
      opened_ = nvs_open("door_setup", NVS_READWRITE, &handle_) == ESP_OK;
      if (!opened_) return Load::Error;
    }
    uint8_t record[kSize]{};
    size_t length = kSize;
    const auto result = nvs_get_blob(handle_, "settings", record, &length);
    if (result == ESP_ERR_NVS_NOT_FOUND) return Load::Missing;
    const bool legacy = length == kLegacySize && record[2] == 1;
    const bool version2 = length == kVersion2Size && record[2] == 2;
    if (result != ESP_OK || (!legacy && !version2 && (length != kSize || record[2] != 3)) ||
        record[0] != 'D' || record[1] != 'S' || record[3] != 0) return Load::Error;
    const uint32_t crc = checksum(record, length - 4);
    for (int i = 0; i < 4; ++i)
      if (record[length - 4 + i] != uint8_t(crc >> (8 * i))) return Load::Error;
    door::DeviceSettings candidate{};
    std::memcpy(candidate.ssid, record + 4, 33);
    std::memcpy(candidate.wifiPassword, record + 37, 65);
    std::memcpy(candidate.pairingCode, record + 102, 9);
    std::memcpy(candidate.setupPassword, record + 111, 64);
    if (!legacy) {
      candidate.relayPulseMs = 0;
      for (int i = 0; i < 4; ++i) candidate.relayPulseMs |= uint32_t(record[175 + i]) << (8 * i);
    }
    if (!legacy && !version2) {
      candidate.nfcMaxFailures = candidate.nfcLockoutSeconds = 0;
      for (int i = 0; i < 4; ++i) {
        candidate.nfcMaxFailures |= uint32_t(record[179 + i]) << (8 * i);
        candidate.nfcLockoutSeconds |= uint32_t(record[183 + i]) << (8 * i);
      }
    }
    if (!door::validSettings(candidate)) return Load::Error;
    settings = candidate;
    return Load::Loaded;
  }
  bool save(const door::DeviceSettings& settings) {
    if (!opened_ || !door::validSettings(settings)) return false;
    uint8_t record[kSize]{};
    record[0] = 'D'; record[1] = 'S'; record[2] = 3;
    std::memcpy(record + 4, settings.ssid, 33);
    std::memcpy(record + 37, settings.wifiPassword, 65);
    std::memcpy(record + 102, settings.pairingCode, 9);
    std::memcpy(record + 111, settings.setupPassword, 64);
    for (int i = 0; i < 4; ++i) record[175 + i] = uint8_t(settings.relayPulseMs >> (8 * i));
    for (int i = 0; i < 4; ++i) {
      record[179 + i] = uint8_t(settings.nfcMaxFailures >> (8 * i));
      record[183 + i] = uint8_t(settings.nfcLockoutSeconds >> (8 * i));
    }
    const uint32_t crc = checksum(record, kSize - 4);
    for (int i = 0; i < 4; ++i) record[kSize - 4 + i] = uint8_t(crc >> (8 * i));
    if (nvs_set_blob(handle_, "settings", record, kSize) != ESP_OK || nvs_commit(handle_) != ESP_OK)
      return false;
    uint8_t verify[kSize]{};
    size_t length = kSize;
    return nvs_get_blob(handle_, "settings", verify, &length) == ESP_OK && length == kSize &&
           std::memcmp(record, verify, kSize) == 0;
  }

 private:
  // Explicit wire layout avoids compiler padding and preserves v1 credentials.
  static constexpr size_t kLegacySize = 4 + 171 + 4;
  static constexpr size_t kVersion2Size = kLegacySize + 4;
  static constexpr size_t kSize = kVersion2Size + 8;
  static uint32_t checksum(const uint8_t* data, size_t size) {
    uint32_t crc = 0xffffffffu;
    for (size_t i = 0; i < size; ++i) {
      crc ^= data[i];
      for (int bit = 0; bit < 8; ++bit)
        crc = (crc >> 1) ^ ((crc & 1) ? 0xedb88320u : 0u);
    }
    return ~crc;
  }
  nvs_handle_t handle_ = 0;
  bool opened_ = false;
};
