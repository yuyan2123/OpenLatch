#include "Protocol.h"
#include "ReaderSession.h"
#include "Ecp.h"
#include "PnReader.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_netif.h"
#include "esp_now.h"
#include "esp_random.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "mbedtls/md.h"
#include "nvs_flash.h"
#include <cstring>
#include <memory>
#include <vector>

#if __has_include("credentials.h")
#include "credentials.h"
#else
#include "credentials.example.h"
#endif

namespace {
using namespace outdoor;
constexpr const char* kLog = "Outdoor";
uint32_t nowMs() { return uint32_t(esp_timer_get_time() / 1000); }
static_assert(kMaxWireSize <= ESP_NOW_MAX_DATA_LEN_V2, "ESP-NOW v2 is required");

class Hmac final : public Auth {
 public:
  bool sign(const uint8_t* data, size_t length, uint8_t* tag) override {
    std::array<uint8_t, 32> digest{};
    const auto* info = mbedtls_md_info_from_type(MBEDTLS_MD_SHA256);
    if (!info || mbedtls_md_hmac(info, credentials::hmacKey.data(), credentials::hmacKey.size(),
                                 data, length, digest.data())) return false;
    std::copy_n(digest.begin(), kTagSize, tag);
    return true;
  }
};

struct Received { uint16_t length; uint8_t channel; std::array<uint8_t, kMaxWireSize> bytes; };
QueueHandle_t inbox = nullptr;
SemaphoreHandle_t sent = nullptr;
uint8_t currentChannel = 1;

void receiveCallback(const esp_now_recv_info_t* info, const uint8_t* data, int length) {
  if (!inbox || !info || !info->src_addr || !info->des_addr || !info->rx_ctrl || !data ||
      length < int(kHeaderSize + kTagSize) || length > int(kMaxWireSize) ||
      std::memcmp(info->src_addr, credentials::indoorMac.data(), 6) ||
      std::memcmp(info->des_addr, credentials::outdoorMac.data(), 6)) return;
  Received message{};
  message.length = uint16_t(length); message.channel = info->rx_ctrl->channel;
  std::copy_n(data, size_t(length), message.bytes.begin());
  // Drop when full. No PN532 work, HMAC, allocation, or blocking in Wi-Fi callbacks.
  (void)xQueueSend(inbox, &message, 0);
}
void sendCallback(const esp_now_send_info_t*, esp_now_send_status_t) {
  if (sent) xSemaphoreGive(sent);
}

class Radio final : public Output {
 public:
  explicit Radio(Codec& codec) : codec_(codec) {}
  void send(const Frame& frame) override {
    if (!codec_.encode(frame, false, wire_)) return;
    // One in-flight send keeps callback ownership and packet order unambiguous.
    (void)xSemaphoreTake(sent, 0);
    if (esp_now_send(credentials::indoorMac.data(), wire_.bytes.data(), wire_.length) != ESP_OK) return;
    if (xSemaphoreTake(sent, pdMS_TO_TICKS(500)) != pdTRUE) {
      // Never allow a late send callback to acknowledge a subsequent send.
      ESP_LOGE(kLog, "Radio callback timed out; restarting transport");
      esp_restart();
    }
    // MAC delivery is not application acknowledgment or authorization.
  }
  Nonce randomNonce() override {
    Nonce nonce{};
    do { esp_fill_random(nonce.data(), nonce.size()); } while (!nonzero(nonce));
    return nonce;
  }
 private:
  Codec& codec_;
  Wire wire_{};
};

template<size_t N> bool any(const std::array<uint8_t, N>& data) {
  uint8_t value = 0; for (auto b : data) value |= b; return value != 0;
}
bool configured() {
  return credentials::provisioned && any(credentials::indoorMac) && any(credentials::outdoorMac) &&
      !(credentials::indoorMac[0] & 1) && !(credentials::outdoorMac[0] & 1) &&
      credentials::indoorMac != credentials::outdoorMac && any(credentials::pmk) &&
      any(credentials::lmk) && any(credentials::hmacKey) && credentials::channels &&
      !(credentials::channels & ~uint16_t(0x1fff));
}
bool setupRadio() {
  ESP_ERROR_CHECK(esp_netif_init());
  ESP_ERROR_CHECK(esp_event_loop_create_default());
  wifi_init_config_t config = WIFI_INIT_CONFIG_DEFAULT();
  ESP_ERROR_CHECK(esp_wifi_init(&config));
  ESP_ERROR_CHECK(esp_wifi_set_storage(WIFI_STORAGE_RAM));
  ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
  ESP_ERROR_CHECK(esp_wifi_start());
  ESP_ERROR_CHECK(esp_wifi_set_ps(WIFI_PS_NONE));
  Mac own{};
  ESP_ERROR_CHECK(esp_wifi_get_mac(WIFI_IF_STA, own.data()));
  ESP_LOGI(kLog, "Station MAC: " MACSTR, MAC2STR(own.data()));
  if (!configured()) {
    ESP_LOGW(kLog, "Not provisioned; radio protocol and NFC are disabled");
    return false;
  }
  if (own != credentials::outdoorMac) {
    ESP_LOGE(kLog, "Credential profile belongs to a different board; disabled");
    return false;
  }
  // Taiwan deployment. The provisioner can restrict the scan to a channel subset.
  ESP_ERROR_CHECK(esp_wifi_set_country_code("TW", false));
  inbox = xQueueCreate(8, sizeof(Received));
  sent = xSemaphoreCreateBinary();
  if (!inbox || !sent) return false;
  ESP_ERROR_CHECK(esp_now_init());
  uint32_t version = 0;
  ESP_ERROR_CHECK(esp_now_get_version(&version));
  if (version < 2) { ESP_LOGE(kLog, "ESP-NOW v2 required"); return false; }
  ESP_ERROR_CHECK(esp_now_set_pmk(credentials::pmk.data()));
  esp_now_peer_info_t peer{};
  std::copy(credentials::indoorMac.begin(), credentials::indoorMac.end(), peer.peer_addr);
  std::copy(credentials::lmk.begin(), credentials::lmk.end(), peer.lmk);
  peer.ifidx = WIFI_IF_STA; peer.encrypt = true; peer.channel = 0;
  ESP_ERROR_CHECK(esp_now_add_peer(&peer));
  ESP_ERROR_CHECK(esp_now_register_recv_cb(receiveCallback));
  ESP_ERROR_CHECK(esp_now_register_send_cb(sendCallback));
  return true;
}
void nextChannel() {
  for (int i = 0; i < 13; ++i) {
    currentChannel = uint8_t(currentChannel % 13 + 1);
    if (credentials::channels & (uint16_t(1) << (currentChannel - 1))) {
      ESP_ERROR_CHECK(esp_wifi_set_channel(currentChannel, WIFI_SECOND_CHAN_NONE));
      return;
    }
  }
}
}  // namespace

extern "C" void app_main() {
  const esp_err_t nvs = nvs_flash_init();
  if (nvs != ESP_OK) {
    // Do not erase NVS automatically, including on partition/version errors.
    ESP_LOGE(kLog, "NVS unavailable: %s; disabled", esp_err_to_name(nvs));
    return;
  }
  if (!setupRadio()) return;
  static Hmac auth;
  static Codec codec(auth, credentials::indoorMac, credentials::outdoorMac);
  static Radio radio(codec);
  static PnReader pn;
  static ReaderSession session(pn, radio);
  static Received message{};
  static Frame frame{};
  session.begin(nowMs());
  uint32_t hoppedAt = nowMs() - 500, healthAt = nowMs();
  std::vector<uint8_t> uid;
  std::array<uint8_t, 2> atqa{};
  uint8_t sak = 0;
  ESP_LOGI(kLog, "NFC relay ready; waiting for authenticated indoor configuration");
  while (true) {
    session.tick(nowMs());
    // Process a bounded number of packets so traffic cannot starve timeouts.
    for (int budget = 0; budget < 4 && xQueueReceive(inbox, &message, 0) == pdTRUE; ++budget) {
      if (message.channel == currentChannel &&
          codec.decode(message.bytes.data(), message.length, true, frame)) session.receive(nowMs(), frame);
    }
    const uint32_t now = nowMs();
    if (!session.bound()) {
      if (uint32_t(now - hoppedAt) >= 500) {
        nextChannel(); hoppedAt = now;
        session.hello();
      }
    } else if (!session.active()) {
      if (uint32_t(now - healthAt) >= 2000) {
        healthAt = now;
        if (!pn.healthy()) { session.begin(nowMs()); continue; }
      }
      const bool present = pn.poll(uid, atqa, sak);
      if (session.rearming()) {
        session.presence(nowMs(), present);
        if (present) pn.release();
      } else if (present) {
        if (!session.detected(nowMs(), uid.data(), uid.size(), atqa, sak)) pn.release();
      }
    }
    vTaskDelay(pdMS_TO_TICKS(5));
  }
}
