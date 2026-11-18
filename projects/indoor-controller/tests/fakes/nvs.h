#pragma once
#include <cstdint>
#include <cstddef>
#include <cstring>
#include <vector>
using nvs_handle_t = uint32_t;
#include "esp_err.h"
constexpr int NVS_READWRITE = 1;
inline std::vector<uint8_t> nvsRecord, nvsPending;
// A separate slot models the additional setup-button namespace without
// changing the legacy tests' direct corruption access to nvsRecord.
inline std::vector<uint8_t> nvsSetupIrRecord, nvsSetupIrPending;
inline bool nvsOpenFails = false, nvsReadFails = false, nvsWriteFails = false, nvsCommitFails = false;
inline bool nvsCorruptReadback = false;
inline int nvsWrites = 0;
inline esp_err_t nvs_open(const char* name, int, nvs_handle_t* handle) {
  *handle = std::strcmp(name, "door_setup_ir") == 0 ? 2 : 1;
  return nvsOpenFails ? ESP_FAIL : ESP_OK;
}
inline esp_err_t nvs_get_blob(nvs_handle_t handle, const char*, void* out, size_t* size) {
  if (nvsReadFails) return ESP_FAIL;
  const auto& record = handle == 2 ? nvsSetupIrRecord : nvsRecord;
  if (record.empty()) return ESP_ERR_NVS_NOT_FOUND;
  if (*size < record.size()) return ESP_FAIL;
  *size = record.size();
  std::memcpy(out, record.data(), record.size());
  if (nvsCorruptReadback) static_cast<uint8_t*>(out)[0] ^= 1;
  return ESP_OK;
}
inline esp_err_t nvs_set_blob(nvs_handle_t handle, const char*, const void* data, size_t size) {
  ++nvsWrites;
  if (nvsWriteFails) return ESP_FAIL;
  const auto* bytes = static_cast<const uint8_t*>(data);
  auto& pending = handle == 2 ? nvsSetupIrPending : nvsPending;
  pending.assign(bytes, bytes + size);
  return ESP_OK;
}
inline esp_err_t nvs_commit(nvs_handle_t handle) {
  if (nvsCommitFails) return ESP_FAIL;
  if (handle == 2) nvsSetupIrRecord = nvsSetupIrPending;
  else nvsRecord = nvsPending;
  return ESP_OK;
}
