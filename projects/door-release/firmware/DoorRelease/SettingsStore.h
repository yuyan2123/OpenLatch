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
    if (result != ESP_OK || length != kSize || record[0] != 'D' || record[1] != 'S' ||
        record[2] != 1 || record[3] != 0) return Load::Error;
    const uint32_t crc = checksum(record, kSize - 4);
    for (int i = 0; i < 4; ++i)
      if (record[kSize - 4 + i] != uint8_t(crc >> (8 * i))) return Load::Error;
    door::DeviceSettings candidate{};
    std::memcpy(&candidate, record + 4, sizeof(candidate));
    if (!door::validSettings(candidate)) return Load::Error;
    settings = candidate;
    return Load::Loaded;
  }
  bool save(const door::DeviceSettings& settings) {
    if (!opened_ || !door::validSettings(settings)) return false;
    uint8_t record[kSize]{};
    record[0] = 'D'; record[1] = 'S'; record[2] = 1;
    std::memcpy(record + 4, &settings, sizeof(settings));
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
  static_assert(sizeof(door::DeviceSettings) == 171, "Settings must contain only fixed character arrays.");
  static constexpr size_t kSize = 4 + sizeof(door::DeviceSettings) + 4;
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
