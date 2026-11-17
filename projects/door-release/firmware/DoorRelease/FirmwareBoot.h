#pragma once
#include <esp_ota_ops.h>
#include <cstdio>
#include <cstdint>

// Arduino's early automatic confirmation is disabled by verifyRollbackLater().
class FirmwareBoot {
 public:
  void begin(uint32_t now) {
    beganAt_ = now; stable_ = healthy_ = false; sha256_[0] = 0;
    const auto* running = esp_ota_get_running_partition();
    uint8_t digest[32]{};
    identified_ = running && esp_partition_get_sha256(running, digest) == ESP_OK;
    if (identified_) for (size_t i = 0; i < sizeof(digest); ++i) snprintf(sha256_ + 2 * i, 3, "%02x", digest[i]);
    esp_ota_img_states_t state{};
    pending_ = running && esp_ota_get_state_partition(running, &state) == ESP_OK && state == ESP_OTA_IMG_PENDING_VERIFY;
  }
  void poll(uint32_t now, bool checksPassed) {
    if (healthy_) return;
    if (checksPassed && identified_) {
      if (!stable_) { stable_ = true; stableAt_ = now; }
      if (uint32_t(now - stableAt_) >= 3000) {
        if (!pending_ || esp_ota_mark_app_valid_cancel_rollback() == ESP_OK) {
          pending_ = false; healthy_ = true; return;
        }
      }
    } else stable_ = false;
    if (pending_ && uint32_t(now - beganAt_) >= 30000) esp_ota_mark_app_invalid_rollback_and_reboot();
  }
  bool healthy() const { return healthy_; }
  bool pending() const { return pending_; }
  const char* imageSha256() const { return sha256_; }
 private:
  char sha256_[65]{};
  uint32_t beganAt_ = 0, stableAt_ = 0;
  bool identified_ = false, pending_ = false, stable_ = false, healthy_ = false;
};
