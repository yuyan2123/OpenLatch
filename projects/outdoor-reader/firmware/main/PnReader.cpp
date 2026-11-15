#include "PnReader.h"
#include "Ecp.h"
#include "PnResponse.h"
#include "esp_log.h"
#include "sdkconfig.h"
#include <algorithm>

namespace outdoor {
// RF initialization and ECP-before-poll behavior follow upstream Pn532Reader.
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
      frontend_->RFConfiguration(0x02, {0x00, 0x0b, 0x10}) != Status::SUCCESS ||
      frontend_->RFConfiguration(0x04, {0xff}) != Status::SUCCESS) {
    ESP_LOGE("PN532", "Reader unavailable; check SPI mode, wiring and power");
    frontend_.reset(); transport_.reset();
    return false;
  }
  ready_ = true;
  ESP_LOGI("PN532", "Reader ready");
  return true;
}
bool PnReader::exchange(const uint8_t* command, size_t length, uint8_t* response, size_t& size) {
  if (!ready_ || !command || length < 4 || length > kMaxCommand) return false;
  std::vector<uint8_t> result;
  if (frontend_->InDataExchange(pn532::span<const uint8_t>(command, length), result, 500) != pn532::Status::SUCCESS ||
      !validPnResponse(result.data(), result.size(), size)) return false;
  size = result.size() - 2;
  std::copy(result.begin() + 2, result.end(), response);
  return true;
}
void PnReader::release() {
  if (ready_) { (void)frontend_->InRelease(1); (void)frontend_->setPassiveActivationRetries(0); }
}
bool PnReader::poll(std::vector<uint8_t>& uid, std::array<uint8_t, 2>& atqa, uint8_t& sak) {
  if (!ready_) return false;
  std::vector<uint8_t> ignored;
  (void)frontend_->InCommunicateThru(ecp_, ignored, 50);
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
