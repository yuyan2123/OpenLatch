#include "Protocol.h"
#include "ChannelScan.h"
#include "ReaderSession.h"
#include "LanTransport.h"
#include "Ecp.h"
#include "PnReader.h"
#include "TapLed.h"
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
#include <atomic>
#include <cstring>
#include <memory>
#include <vector>

#if __has_include("credentials.h")
#include "credentials.h"
#else
#include "credentials.example.h"
#endif

#if __has_include("reader_wifi.h")
#include "reader_wifi.h"
#else
namespace reader_wifi {
inline constexpr std::array<uint8_t, 0> ssid{}, password{};
}
#endif

namespace {
using namespace outdoor;
constexpr const char* kLog = "Outdoor";
constexpr uint32_t kJoinMs = 8000, kLanProbeMs = 3500, kWifiLossMs = 1000, kWifiRetryMs = 30000;
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
std::atomic<unsigned> rxQueueFull{0}, txDeliveryFailed{0};
unsigned txSubmitFailed = 0;  // Worker-owned.
esp_netif_t* stationInterface = nullptr;

void receiveCallback(const esp_now_recv_info_t* info, const uint8_t* data, int length) {
  if (!inbox || !info || !info->src_addr || !info->des_addr || !info->rx_ctrl || !data ||
      length < int(kHeaderSize + kTagSize) || length > int(kMaxWireSize) ||
      std::memcmp(info->src_addr, credentials::indoorMac.data(), 6) ||
      std::memcmp(info->des_addr, credentials::outdoorMac.data(), 6)) return;
  Received message{};
  message.length = uint16_t(length); message.channel = info->rx_ctrl->channel;
  std::copy_n(data, size_t(length), message.bytes.begin());
  // Drop when full. No PN532 work, HMAC, allocation, or blocking in Wi-Fi callbacks.
  if (xQueueSend(inbox, &message, 0) != pdTRUE) rxQueueFull.fetch_add(1, std::memory_order_relaxed);
}
void sendCallback(const esp_now_send_info_t*, esp_now_send_status_t status) {
  if (status != ESP_NOW_SEND_SUCCESS) txDeliveryFailed.fetch_add(1, std::memory_order_relaxed);
  if (sent) xSemaphoreGive(sent);
}

class Radio final : public Output {
 public:
  Radio(Codec& codec, LanTransport& lan) : codec_(codec), lan_(lan) {}
  void route(Link link, LanPeer peer = {}) { link_ = link; peer_ = peer; }
  void send(const Frame& frame) override {
    if (!codec_.encode(frame, false, wire_)) return;
    if (link_ == Link::Wifi) { (void)lan_.send(wire_, peer_); return; }
    if (link_ != Link::EspNow) return;
    // One in-flight send keeps callback ownership and packet order unambiguous.
    (void)xSemaphoreTake(sent, 0);
    if (esp_now_send(credentials::indoorMac.data(), wire_.bytes.data(), wire_.length) != ESP_OK) {
      ++txSubmitFailed;
      return;
    }
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
  LanTransport& lan_;
  Link link_ = Link::None;
  LanPeer peer_{};
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
  if (!reader_wifi::ssid.empty()) stationInterface = esp_netif_create_default_wifi_sta();
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
bool joinWifi() {
  if (!stationInterface) return false;
  wifi_config_t config{};
  std::copy(reader_wifi::ssid.begin(), reader_wifi::ssid.end(), config.sta.ssid);
  std::copy(reader_wifi::password.begin(), reader_wifi::password.end(), config.sta.password);
  return esp_wifi_set_config(WIFI_IF_STA, &config) == ESP_OK && esp_wifi_connect() == ESP_OK;
}

struct Station {
  uint32_t address = 0;
  uint8_t channel = 0;
};

Station wifiStation() {
  if (!stationInterface) return {};
  wifi_ap_record_t accessPoint{};
  if (esp_wifi_sta_get_ap_info(&accessPoint) != ESP_OK) return {};
  esp_netif_ip_info_t ip{};
  const auto result = esp_netif_get_ip_info(stationInterface, &ip);
  return {result == ESP_OK ? ip.ip.addr : 0, accessPoint.primary};
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
  static TapLed tapLed(GPIO_NUM_2, true);
  if (!tapLed.begin()) ESP_LOGW(kLog, "Tap LED unavailable");
  static Hmac auth;
  static Codec codec(auth, credentials::indoorMac, credentials::outdoorMac);
  static LanTransport lan(credentials::hmacKey, false);
  static Radio radio(codec, lan);
  static PnReader pn;
  static ReaderSession session(pn, radio);
  static Received message{};
  static Frame frame{};
  static Wire incomingWifi{};
  session.begin(nowMs());
  ChannelScan scan(credentials::channels);
  uint32_t tapStartedAt = 0;
  Link link = joinWifi() ? Link::None : Link::EspNow;
  radio.route(link);
  uint32_t modeAt = nowMs(), wifiAttemptAt = modeAt, lastHelloAt = modeAt - 500;
  uint32_t healthAt = modeAt, statsAt = modeAt;
  uint32_t wifiAddress = 0, lastWifiAt = modeAt;
  bool wifiEstablished = false;
  LanPeer lanPeer{};
  const auto changeLink = [&](Link next, uint32_t now) {
    if (link == next) return;
    session.begin(now);
    if (next != Link::Wifi) { lan.stop(); lanPeer = {}; }
    wifiEstablished = false;
    link = next;
    modeAt = now;
    lastWifiAt = now;
    lastHelloAt = now - 500;
    radio.route(next);
    if (next == Link::Wifi) ESP_LOGI(kLog, "Joined Wi-Fi; looking for the indoor controller");
  };
  std::vector<uint8_t> uid;
  std::array<uint8_t, 2> atqa{};
  uint8_t sak = 0;
  ESP_LOGI(kLog, "NFC relay ready; waiting for authenticated indoor configuration");
  while (true) {
    const bool tapWasActive = session.active();
    const uint32_t beforeReceive = nowMs();
    session.tick(beforeReceive);
    const auto station = wifiStation();
    const uint32_t address = station.address;
    if (link == Link::None) {
      if (address && lan.begin()) {
        wifiAddress = address;
        changeLink(Link::Wifi, beforeReceive);
      } else if (uint32_t(beforeReceive - modeAt) >= kJoinMs) {
        (void)esp_wifi_disconnect();
        changeLink(Link::EspNow, beforeReceive);
      }
    } else if (link == Link::Wifi) {
      if (address) {
        lastWifiAt = beforeReceive;
        if (address != wifiAddress || (wifiEstablished && !session.bound())) {
          wifiAddress = address;
          wifiEstablished = false;
          session.begin(beforeReceive);
          lanPeer = {};
          radio.route(Link::Wifi);
          modeAt = beforeReceive;
          lastHelloAt = beforeReceive - 500;
          ESP_LOGI(kLog, "Renewing the reader session over Wi-Fi");
        }
      }
      if (uint32_t(beforeReceive - lastWifiAt) >= kWifiLossMs ||
          (!session.bound() && uint32_t(beforeReceive - modeAt) >= kLanProbeMs)) {
        ESP_LOGW(kLog, "Wi-Fi reader connection unavailable; trying ESP-NOW");
        changeLink(Link::EspNow, beforeReceive);
      }
    } else if (!session.bound()) {
      if (stationInterface && uint32_t(beforeReceive - wifiAttemptAt) >= kWifiRetryMs) {
        wifiAttemptAt = beforeReceive;
        if (address && lan.begin()) {
          wifiAddress = address;
          changeLink(Link::Wifi, beforeReceive);
        } else if (joinWifi()) changeLink(Link::None, beforeReceive);
      } else if (station.channel && uint32_t(beforeReceive - modeAt) >= kLeaseMs) (void)esp_wifi_disconnect();
    }

    if (link == Link::Wifi) {
      LanPeer peer;
      for (int budget = 0; budget < 4 && lan.receive(incomingWifi, peer); ++budget) {
        if (!codec.decode(incomingWifi.bytes.data(), incomingWifi.length, true, frame)) continue;
        if (session.bound()) {
          if (peer == lanPeer) session.receive(nowMs(), frame);
        } else if (frame.type == Type::Configure && frame.outdoor == session.nonce()) {
          radio.route(link, peer);
          session.receive(nowMs(), frame);
          if (session.bound()) {
            wifiEstablished = true;
            lanPeer = peer;
            ESP_LOGI(kLog, "Reader linked over Wi-Fi");
          } else radio.route(link);
        }
      }
    }
    // Process a bounded number of packets so traffic cannot starve timeouts.
    const uint8_t espNowChannel = station.channel ? station.channel : scan.current();
    for (int budget = 0; budget < 4 && xQueueReceive(inbox, &message, 0) == pdTRUE; ++budget) {
      if (link == Link::EspNow && message.channel == espNowChannel &&
          codec.decode(message.bytes.data(), message.length, true, frame)) {
        const bool wasConnected = session.bound();
        session.receive(nowMs(), frame);
        if (!wasConnected && session.bound()) {
          scan.remember(message.channel);
          ESP_LOGI(kLog, "Reader linked over ESP-NOW");
        }
      }
    }
    const uint32_t now = nowMs();
    if (tapWasActive && !session.active()) {
      ESP_LOGI(kLog, "Reader tap ended: duration_ms=%lu", static_cast<unsigned long>(now - tapStartedAt));
    }
    if (link == Link::EspNow && uint32_t(now - statsAt) >= 10000) {
      statsAt = now;
      ESP_LOGI(kLog, "ESP-NOW channel=%u bound=%d rx_queue_full=%u tx_submit_failed=%u tx_delivery_failed=%u",
               unsigned(espNowChannel), int(session.bound()), rxQueueFull.load(std::memory_order_relaxed),
               txSubmitFailed, txDeliveryFailed.load(std::memory_order_relaxed));
    }
    if (link == Link::EspNow && session.bound()) (void)scan.probeDue(now, true);
    if (!session.bound()) {
      if (link == Link::EspNow && !station.channel) {
        if (scan.probeDue(now, false) && esp_wifi_set_channel(scan.current(), WIFI_SECOND_CHAN_NONE) == ESP_OK)
          session.hello();
      } else if (link != Link::None && uint32_t(now - lastHelloAt) >= 500) {
        lastHelloAt = now;
        if (link == Link::Wifi) radio.route(link);
        session.hello();
      }
    } else if (!session.active()) {
      if (uint32_t(now - healthAt) >= 2000) {
        healthAt = now;
        if (!pn.healthy()) { session.begin(nowMs()); continue; }
      }
      // Only advertise Home Key when ready to handle a new tap.
      const bool retry = session.prepareRetry(now);
      const bool waitingForRemoval = session.rearming() && !retry;
      const bool present = pn.poll(uid, atqa, sak, !waitingForRemoval);
      if (waitingForRemoval) {
        session.presence(nowMs(), present);
        if (present) pn.release();
      } else if (present) {
        tapStartedAt = nowMs();
        if (session.detected(tapStartedAt, uid.data(), uid.size(), atqa, sak)) {
          if (retry) ESP_LOGI(kLog, "Reader tap restarted after RF error");
          else tapLed.flash();
        } else pn.release();
      }
    }
    vTaskDelay(pdMS_TO_TICKS(session.active() ? 1 : 5));
  }
}
