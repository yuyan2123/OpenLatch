#include "PnReader.h"
#include "Ecp.h"
#include "PnResponse.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "sdkconfig.h"
#include <algorithm>

namespace outdoor {
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
bool PnReader::restartField() {
  if (!ready_ || frontend_->RFConfiguration(0x01, {0x02}) != pn532::Status::SUCCESS) return false;
  vTaskDelay(pdMS_TO_TICKS(50));
  if (frontend_->RFConfiguration(0x01, {0x03}) != pn532::Status::SUCCESS) {
    ready_ = false;
    return false;
  }
  vTaskDelay(pdMS_TO_TICKS(5));
  return true;
}
bool PnReader::poll(std::vector<uint8_t>& uid, std::array<uint8_t, 2>& atqa, uint8_t& sak, bool announce) {
  if (!ready_) return false;
  if (announce) {
    std::vector<uint8_t> ignored;
    (void)frontend_->InCommunicateThru(ecp_, ignored, 50);
  }
  uid.clear();
  return frontend_->InListPassiveTarget(0x00, uid, atqa, sak, 50) == pn532::Status::SUCCESS;
}
bool PnReader::healthy() {
  if (!ready_) return false;
  if (frontend_->WriteRegister({0x63, 0x3d, 0}) == pn532::Status::SUCCESS) return true;
  ready_ = false; frontend_.reset(); transport_.reset();
  ESP_LOGW("PN532", "Reader lost; a fresh authenticated configuration is required");
  return false;
}
}
