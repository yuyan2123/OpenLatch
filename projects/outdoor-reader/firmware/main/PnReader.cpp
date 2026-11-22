#include "PnReader.h"
#include "Ecp.h"
#include "PnResponse.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "sdkconfig.h"
#include <algorithm>

namespace outdoor {
namespace {
constexpr uint32_t kBetweenIsoDepTapsMs = 800, kRetryFieldOffMs = 350;
uint32_t clockMs() { return uint32_t(esp_timer_get_time() / 1000); }
}
// ECP is a broadcast. A 6.4 ms RF timeout and zero retries let the PN532 finish
// it before passive polling, without a host abort. ISO-DEP uses the target's FWT.
bool PnReader::configure(const std::array<uint8_t, 8>& group) {
  ecp_ = makeEcp(group);
  if (ready_) return true;
  frontend_.reset(); transport_.reset();
  transport_ = std::make_unique<pn532::SpiTransport>(GPIO_NUM_NC,
      gpio_num_t(CONFIG_PN532_MISO), gpio_num_t(CONFIG_PN532_MOSI),
      gpio_num_t(CONFIG_PN532_SCK), gpio_num_t(CONFIG_PN532_SS));
  frontend_ = std::make_unique<pn532::Frontend>(*transport_);
  using pn532::Status;
  if (frontend_->begin() != Status::SUCCESS || !frontend_->GetFirmwareVersion() ||
      frontend_->RFConfiguration(0x01, {0x03}) != Status::SUCCESS ||
      frontend_->setPassiveActivationRetries(0) != Status::SUCCESS ||
      frontend_->RFConfiguration(0x02, {0x00, 0x0b, 0x07}) != Status::SUCCESS ||
      frontend_->RFConfiguration(0x04, {0x00}) != Status::SUCCESS) {
    ESP_LOGE("PN532", "Reader unavailable; check SPI mode, wiring and power");
    frontend_.reset(); transport_.reset();
    return false;
  }
  ready_ = true;
  fieldPaused_ = false;
  ESP_LOGI("PN532", "Reader ready");
  return true;
}
ExchangeResult PnReader::exchange(const uint8_t* command, size_t length, uint8_t* response, size_t& size) {
  if (!ready_ || !command || length < 4 || length > kMaxCommand) return ExchangeResult::Failed;
  std::vector<uint8_t> result;
  const auto status = frontend_->InDataExchange(pn532::span<const uint8_t>(command, length), result, 500);
  if (status != pn532::Status::SUCCESS || !validPnResponse(result.data(), result.size(), size)) {
    const int rfStatus = result.size() >= 2 && result[0] == 0x41 ? result[1] : -1;
    ESP_LOGW("PN532", "APDU exchange failed: ins=0x%02x driver=%u rf_status=%d response_bytes=%u",
             unsigned(command[1]), unsigned(status), rfStatus, unsigned(result.size()));
    return status == pn532::Status::SUCCESS && retryablePnRfError(result.data(), result.size()) ?
        ExchangeResult::RetryableRfError : ExchangeResult::Failed;
  }
  size = result.size() - 2;
  std::copy(result.begin() + 2, result.end(), response);
  return ExchangeResult::Ok;
}
void PnReader::release() {
  if (ready_) { (void)frontend_->InRelease(1); (void)frontend_->setPassiveActivationRetries(0); }
}
void PnReader::endTap(bool isoDep) {
  release();
  if (isoDep) (void)pauseField(kBetweenIsoDepTapsMs);
}
bool PnReader::pauseField(uint32_t duration) {
  if (!ready_) return false;
  if (!fieldPaused_) {
    if (frontend_->RFConfiguration(0x01, {0x02}) != pn532::Status::SUCCESS) return false;
    fieldOffAt_ = clockMs();
    fieldPaused_ = true;
  }
  fieldPauseMs_ = duration;
  return true;
}
bool PnReader::restartField() {
  return pauseField(kRetryFieldOffMs);
}
std::optional<bool> PnReader::poll(std::vector<uint8_t>& uid, std::array<uint8_t, 2>& atqa, uint8_t& sak) {
  if (!ready_) return std::nullopt;
  using pn532::Status;
  if (fieldPaused_) {
    if (uint32_t(clockMs() - fieldOffAt_) < fieldPauseMs_) return std::nullopt;
    if (frontend_->RFConfiguration(0x01, {0x03}) != Status::SUCCESS) {
      ready_ = false;
      return std::nullopt;
    }
    fieldPaused_ = false;
    vTaskDelay(pdMS_TO_TICKS(5));
  }
  // Passive activation can leave short-frame framing or transmit CRC enabled.
  // The ECP payload already contains CRC-A. Send it as full NFC-A bytes at 106 kbit/s.
  std::vector<uint8_t> result;
  if (frontend_->WriteRegister({0x63, 0x02, 0, 0x63, 0x3d, 0}) != Status::SUCCESS ||
      frontend_->InCommunicateThru(ecp_, result, 50) != Status::SUCCESS ||
      !completedEcpBroadcast(result.data(), result.size())) {
    if (!ecpErrorReported_) ESP_LOGW("PN532", "ECP broadcast failed; passive polling deferred");
    ecpErrorReported_ = true;
    return std::nullopt;
  }
  ecpErrorReported_ = false;
  uid.clear();
  const auto status = frontend_->InListPassiveTarget(0x00, uid, atqa, sak, 50);
  if (status == Status::SUCCESS) return true;
  if (status == Status::NO_TAGS_FOUND) return false;
  return std::nullopt;
}
bool PnReader::healthy() {
  if (!ready_) return false;
  if (frontend_->WriteRegister({0x63, 0x3d, 0}) == pn532::Status::SUCCESS) return true;
  ready_ = false; frontend_.reset(); transport_.reset();
  ESP_LOGW("PN532", "Reader lost; a fresh authenticated configuration is required");
  return false;
}
}
