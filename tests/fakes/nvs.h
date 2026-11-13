#pragma once
#include <cstdint>
#include <cstddef>
#include <cstring>
#include <vector>
using nvs_handle_t = uint32_t;
#include "esp_err.h"
constexpr int NVS_READWRITE = 1;
inline std::vector<uint8_t> nvsRecord, nvsPending;
inline bool nvsOpenFails = false, nvsReadFails = false, nvsWriteFails = false, nvsCommitFails = false;
inline bool nvsCorruptReadback = false;
inline int nvsWrites = 0;
inline esp_err_t nvs_open(const char*, int, nvs_handle_t* handle) {
  *handle = 1;
  return nvsOpenFails ? ESP_FAIL : ESP_OK;
}
inline esp_err_t nvs_get_blob(nvs_handle_t, const char*, void* out, size_t* size) {
  if (nvsReadFails) return ESP_FAIL;
  if (nvsRecord.empty()) return ESP_ERR_NVS_NOT_FOUND;
  if (*size < nvsRecord.size()) return ESP_FAIL;
  *size = nvsRecord.size();
  std::memcpy(out, nvsRecord.data(), nvsRecord.size());
  if (nvsCorruptReadback) static_cast<uint8_t*>(out)[0] ^= 1;
  return ESP_OK;
}
inline esp_err_t nvs_set_blob(nvs_handle_t, const char*, const void* data, size_t size) {
  ++nvsWrites;
  if (nvsWriteFails) return ESP_FAIL;
  const auto* bytes = static_cast<const uint8_t*>(data);
  nvsPending.assign(bytes, bytes + size);
  return ESP_OK;
}
inline esp_err_t nvs_commit(nvs_handle_t) {
  if (nvsCommitFails) return ESP_FAIL;
  nvsRecord = nvsPending;
  return ESP_OK;
}
