#include "driver/gpio.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "pn532_cxx/pn532.hpp"
#include "pn532_hal/spi.hpp"
#include "sdkconfig.h"
#include <array>
#include <numeric>
#include <optional>

namespace {
constexpr char kLog[] = "CardTest";
constexpr auto kLed = GPIO_NUM_2;
constexpr uint32_t kPollTimeoutMs = 1100;
using pn532::Status;

std::optional<bool> detectCard(pn532::SpiTransport& transport) {
  // UM0701-02 §7.3.13: one InAutoPoll cycle, 150 ms per type.
  // Scan Type A, FeliCa (212/424 kbps), Type B, and Jewel.
  constexpr uint8_t command[]{
    0x00, 0x00, 0xff, 0x09, 0xf7, 0xd4, 0x60, 0x01,
    0x01, 0x00, 0x01, 0x02, 0x03, 0x04, 0xc0, 0x00
  };
  auto transaction = transport.begin();
  if (!transaction || transaction.write(command) != Status::SUCCESS ||
      transaction.waitForAck() != Status::SUCCESS ||
      transaction.waitForResponse(kPollTimeoutMs) != Status::SUCCESS) return std::nullopt;

  std::array<uint8_t, 5> header{};
  if (transaction.read(header) != Status::SUCCESS ||
      header[0] != 0 || header[1] != 0 || header[2] != 0xff ||
      uint8_t(header[3] + header[4]) != 0 || header[3] < 3 || header[3] == 0xff) {
    return std::nullopt;
  }

  const size_t length = header[3];
  std::array<uint8_t, 256> payload{};
  if (transaction.read({payload.data(), length + 2}) != Status::SUCCESS ||
      std::accumulate(payload.begin(), payload.begin() + length + 1, uint8_t{0}) != 0 ||
      payload[length + 1] != 0 || payload[0] != 0xd5 || payload[1] != 0x61 || payload[2] > 2) {
    return std::nullopt;
  }

  size_t next = 3;
  for (uint8_t target = 0; target < payload[2]; ++target) {
    if (next + 2 > length || payload[next + 1] == 0) return std::nullopt;
    next += 2 + payload[next + 1];
  }
  if (next != length) return std::nullopt;
  return payload[2] != 0;
}

void scanCards(pn532::SpiTransport& transport, pn532::Frontend& reader) {
  const auto samStatus = reader.begin();
  if (samStatus != Status::SUCCESS) {
    ESP_LOGE(kLog, "PN532 did not initialize over SPI (status %u)", unsigned(samStatus));
    return;
  }
  if (!reader.GetFirmwareVersion()) {
    ESP_LOGE(kLog, "PN532 firmware version query failed");
    return;
  }
  if (reader.setPassiveActivationRetries(0) != Status::SUCCESS) {
    ESP_LOGE(kLog, "PN532 passive polling configuration failed");
    return;
  }

  bool ready = false;
  bool present = false;
  while (const auto card = detectCard(transport)) {
    if (!ready) {
      ESP_LOGI(kLog, "PN532 card test ready; LED on GPIO2");
      ready = true;
    }
    if (*card != present) {
      present = *card;
      ESP_ERROR_CHECK(gpio_set_level(kLed, present));
      ESP_LOGI(kLog, "%s", present ? "Card detected" : "Card removed");
    }
    if (present) {
      // Remove HALT/SLEEP state so a held card can be detected again.
      (void)reader.InRelease(0);
      if (reader.RFConfiguration(0x01, {0x02}) != Status::SUCCESS) break;
    }
    vTaskDelay(pdMS_TO_TICKS(30));
  }
  ESP_ERROR_CHECK(gpio_set_level(kLed, 0));
  ESP_LOGW(kLog, "PN532 polling failed; retrying");
}
}

extern "C" void app_main() {
  ESP_ERROR_CHECK(gpio_reset_pin(kLed));
  ESP_ERROR_CHECK(gpio_set_direction(kLed, GPIO_MODE_OUTPUT));
  ESP_ERROR_CHECK(gpio_set_level(kLed, 1));
  vTaskDelay(pdMS_TO_TICKS(300));
  ESP_ERROR_CHECK(gpio_set_level(kLed, 0));
  ESP_LOGI(kLog, "PN532 card test starting");

  pn532::SpiTransport transport(GPIO_NUM_NC, gpio_num_t(CONFIG_PN532_MISO), gpio_num_t(CONFIG_PN532_MOSI),
      gpio_num_t(CONFIG_PN532_SCK), gpio_num_t(CONFIG_PN532_SS));
  pn532::Frontend reader(transport);
  while (true) {
    scanCards(transport, reader);
    vTaskDelay(pdMS_TO_TICKS(1000));
  }
}
