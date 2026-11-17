#pragma once
#include <esp_ota_ops.h>
#include "FirmwareUpdate.h"

class EspFirmwareSink final : public door::FirmwareSink {
 public:
  size_t capacity() const override {
    const auto* first = esp_partition_find_first(ESP_PARTITION_TYPE_APP, ESP_PARTITION_SUBTYPE_APP_OTA_0, nullptr);
    const auto* second = esp_partition_find_first(ESP_PARTITION_TYPE_APP, ESP_PARTITION_SUBTYPE_APP_OTA_1, nullptr);
    const auto* running = esp_ota_get_running_partition();
    const auto* target = esp_ota_get_next_update_partition(nullptr);
    if (!first || !second || !running || !target || running->address == target->address) return 0;
    return std::min(size_t(first->size), size_t(second->size));
  }
  bool begin(size_t size) override {
    if (opened_ || !size || size > capacity()) return false;
    target_ = esp_ota_get_next_update_partition(nullptr);
    opened_ = target_ && esp_ota_begin(target_, size, &handle_) == ESP_OK;
    return opened_;
  }
  bool write(const uint8_t* bytes, size_t size) override {
    return opened_ && esp_ota_write(handle_, bytes, size) == ESP_OK;
  }
  bool finish() override {
    if (!opened_) return false;
    opened_ = false;
    if (esp_ota_end(handle_) != ESP_OK) return false;
    return esp_ota_set_boot_partition(target_) == ESP_OK;
  }
  void abort() override { if (opened_) esp_ota_abort(handle_); opened_ = false; }
 private:
  const esp_partition_t* target_ = nullptr;
  esp_ota_handle_t handle_ = 0;
  bool opened_ = false;
};
