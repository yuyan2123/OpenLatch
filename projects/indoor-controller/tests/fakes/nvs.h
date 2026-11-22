#pragma once
#include <cstdint>
#include <cstddef>
#include <cstring>
#include <map>
#include <string>
#include <vector>
using nvs_handle_t = uint32_t;
#include "esp_err.h"
constexpr int NVS_READWRITE = 1;
inline std::vector<uint8_t> nvsRecord, nvsPending;
// A separate slot models the additional setup-button namespace without
// changing the legacy tests' direct corruption access to nvsRecord.
inline std::vector<uint8_t> nvsSetupIrRecord, nvsSetupIrPending;
inline std::vector<uint8_t> nvsFobRecord, nvsFobPending;
inline std::map<nvs_handle_t, std::map<std::string, std::vector<uint8_t>>> nvsLabels, nvsPendingLabels;
inline bool nvsOpenFails = false, nvsReadFails = false, nvsWriteFails = false, nvsCommitFails = false;
inline bool nvsCorruptReadback = false;
inline int nvsWrites = 0;
inline esp_err_t nvs_open(const char* name, int, nvs_handle_t* handle) {
  *handle = std::strcmp(name, "door_setup_ir") == 0 ? 2 : std::strcmp(name, "door_rfid") == 0 ? 3 : 1;
  return nvsOpenFails ? ESP_FAIL : ESP_OK;
}
inline esp_err_t nvs_get_blob(nvs_handle_t handle, const char* key, void* out, size_t* size) {
  if (nvsReadFails) return ESP_FAIL;
  const auto& record = std::strncmp(key, "label", 5) == 0 ? nvsLabels[handle][key] :
      handle == 2 ? nvsSetupIrRecord : handle == 3 ? nvsFobRecord : nvsRecord;
  if (record.empty()) return ESP_ERR_NVS_NOT_FOUND;
  if (*size < record.size()) return ESP_FAIL;
  *size = record.size();
  std::memcpy(out, record.data(), record.size());
  if (nvsCorruptReadback) static_cast<uint8_t*>(out)[0] ^= 1;
  return ESP_OK;
}
inline esp_err_t nvs_set_blob(nvs_handle_t handle, const char* key, const void* data, size_t size) {
  ++nvsWrites;
  if (nvsWriteFails) return ESP_FAIL;
  const auto* bytes = static_cast<const uint8_t*>(data);
  auto& pending = std::strncmp(key, "label", 5) == 0 ? nvsPendingLabels[handle][key] :
      handle == 2 ? nvsSetupIrPending : handle == 3 ? nvsFobPending : nvsPending;
  pending.assign(bytes, bytes + size);
  return ESP_OK;
}
inline esp_err_t nvs_erase_key(nvs_handle_t handle, const char* key) {
  if (nvsWriteFails) return ESP_FAIL;
  if (nvsLabels[handle][key].empty()) return ESP_ERR_NVS_NOT_FOUND;
  nvsPendingLabels[handle][key].clear();
  return ESP_OK;
}
inline esp_err_t nvs_commit(nvs_handle_t handle) {
  if (!nvsPendingLabels[handle].empty()) {
    if (!nvsCommitFails) {
      for (auto& item : nvsPendingLabels[handle]) nvsLabels[handle][item.first] = std::move(item.second);
    }
    nvsPendingLabels[handle].clear();
    return nvsCommitFails ? ESP_FAIL : ESP_OK;
  }
  if (nvsCommitFails) return ESP_FAIL;
  if (handle == 2) nvsSetupIrRecord = nvsSetupIrPending;
  else if (handle == 3) nvsFobRecord = nvsFobPending;
  else nvsRecord = nvsPending;
  return ESP_OK;
}
