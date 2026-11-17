#pragma once
#include "esp_err.h"
#include <cstddef>
#include <cstdint>
#include <vector>
using esp_ota_handle_t = uint32_t;
constexpr int ESP_PARTITION_TYPE_APP = 0, ESP_PARTITION_SUBTYPE_APP_OTA_0 = 0x10, ESP_PARTITION_SUBTYPE_APP_OTA_1 = 0x11;
struct esp_partition_t { uint32_t address, size; };
inline esp_partition_t otaFirst{0x10000, 0x1e0000}, otaSecond{0x1f0000, 0x1e0000};
inline bool otaDual = true, otaSameTarget = false, otaBeginFails = false, otaWriteFails = false, otaEndFails = false, otaSelectFails = false;
inline int otaBegins = 0, otaWrites = 0, otaEnds = 0, otaAborts = 0, otaSelections = 0;
inline size_t otaExpected = 0;
inline std::vector<uint8_t> otaBytes;
inline void resetOta() {
  otaDual = true; otaSameTarget = otaBeginFails = otaWriteFails = otaEndFails = otaSelectFails = false;
  otaBegins = otaWrites = otaEnds = otaAborts = otaSelections = 0; otaExpected = 0; otaBytes.clear();
}
inline const esp_partition_t* esp_partition_find_first(int, int subtype, const char*) {
  return subtype == ESP_PARTITION_SUBTYPE_APP_OTA_0 ? &otaFirst : otaDual ? &otaSecond : nullptr;
}
inline const esp_partition_t* esp_ota_get_running_partition() { return &otaFirst; }
inline const esp_partition_t* esp_ota_get_next_update_partition(const esp_partition_t*) { return otaSameTarget ? &otaFirst : &otaSecond; }
inline esp_err_t esp_ota_begin(const esp_partition_t*, size_t size, esp_ota_handle_t* handle) {
  ++otaBegins; otaExpected = size; otaBytes.clear(); *handle = 7; return otaBeginFails ? ESP_FAIL : ESP_OK;
}
inline esp_err_t esp_ota_write(esp_ota_handle_t, const void* data, size_t size) {
  ++otaWrites; if (otaWriteFails) return ESP_FAIL;
  const auto* bytes = static_cast<const uint8_t*>(data); otaBytes.insert(otaBytes.end(), bytes, bytes + size); return ESP_OK;
}
inline esp_err_t esp_ota_end(esp_ota_handle_t) { ++otaEnds; return otaEndFails || otaBytes.size() != otaExpected ? ESP_FAIL : ESP_OK; }
inline esp_err_t esp_ota_abort(esp_ota_handle_t) { ++otaAborts; return ESP_OK; }
inline esp_err_t esp_ota_set_boot_partition(const esp_partition_t*) { if (otaSelectFails) return ESP_FAIL; ++otaSelections; return ESP_OK; }
