#pragma once
#include <Arduino.h>
#include <WiFi.h>
#include <esp_now.h>
#include <esp_wifi.h>
#include <esp_random.h>
#include <esp_mac.h>
#include <mbedtls/md.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/task.h>
#include <atomic>
#include <memory>
#include "IndoorSession.h"
#include "HomeKeyStore.h"
#include "HomeKeyCrypto.h"
#include "NfcAttemptLimiter.h"
#include "ddk/homekey/ProfileFactory.h"
#include "ddk/transport/NfcChannel.h"
#if __has_include("nfc_peer.h")
#include "nfc_peer.h"
#else
#include "nfc_peer.example.h"
#endif

class HomeKeyManager {
 public:
  HomeKeyManager(HomeKeyStore& store, door::NfcAttemptLimiter& attempts) : store_(store), attempts_(attempts),
      codec_(auth_, credentials::indoorMac, credentials::outdoorMac) {}
  bool begin() {
    if (!homeKeyCryptoReady()) return false;
    store_.begin();
    if (!credentials::provisioned || credentials::indoorMac == credentials::outdoorMac ||
        !nonzero(credentials::indoorMac) || !nonzero(credentials::outdoorMac) ||
        (credentials::indoorMac[0] & 1) || (credentials::outdoorMac[0] & 1) ||
        !credentials::channels || (credentials::channels & ~uint16_t(0x1fff)) ||
        !nonzero(credentials::pmk) || !nonzero(credentials::lmk) || !nonzero(credentials::hmacKey)) return false;
    uint8_t mac[6]{}; if (esp_read_mac(mac, ESP_MAC_WIFI_STA) != ESP_OK ||
        std::memcmp(mac, credentials::indoorMac.data(), 6)) return false;
    inbox_ = xQueueCreate(8, sizeof(Received)); verified_ = xQueueCreate(1, sizeof(Verified));
    if (!inbox_ || !verified_) return false;
    instance_ = this;
    configured_ = true;
    if (xTaskCreate(workerEntry, "homekey", 14336, this, 1, nullptr) != pdPASS) { configured_ = false; return false; }
    return true;
  }
  void enabled(bool value) { if (enabled_.exchange(value) && !value) ++admission_; }
  bool configured() const { return configured_; }
  bool provisioned() const { return provisioned_; }
  bool connected() const { return connected_; }
  // Main loop consumes each result once. Queued results expire, and any pause
  // or credential edit invalidates results produced before that change.
  bool takeVerified(uint32_t now) {
    Verified result{};
    return verified_ && xQueueReceive(verified_, &result, 0) == pdTRUE && enabled_ &&
           result.admission == admission_ && result.generation == store_.generation() && store_.healthy() &&
           uint32_t(now - result.at) < 250 && connected_;
  }
 private:
  struct Received { uint16_t length; std::array<uint8_t, outdoor::kMaxWireSize> bytes; };
  struct Verified { uint32_t at, generation, admission; };
  template<size_t N> static bool nonzero(const std::array<uint8_t, N>& value) {
    uint8_t bits = 0; for (uint8_t byte : value) bits |= byte; return bits != 0;
  }
  struct Hmac final : outdoor::Auth {
    bool sign(const uint8_t* data, size_t length, uint8_t* tag) override {
      std::array<uint8_t, 32> hash{};
      const auto* algorithm = mbedtls_md_info_from_type(MBEDTLS_MD_SHA256);
      if (!algorithm || mbedtls_md_hmac(algorithm, credentials::hmacKey.data(), credentials::hmacKey.size(), data, length, hash.data())) return false;
      std::copy_n(hash.begin(), outdoor::kTagSize, tag); return true;
    }
  };
  class Channel final : public ddk::NfcChannel {
   public:
    explicit Channel(Callback callback) : NfcChannel(std::move(callback)) {}
    size_t max_command_payload() const override { return 246; }
    ddk::ApduResponse transceive_full(const ddk::ApduCommand& command, bool skip = false, size_t chunk = 255) override {
      return NfcChannel::transceive_full(command, skip, std::min(chunk ? chunk : size_t(246), size_t(246)));
    }
  };
  static void receive(const esp_now_recv_info_t* info, const uint8_t* data, int length) {
    auto* self = instance_;
    if (!self || !info || !info->src_addr || !info->des_addr || !data ||
        length < int(outdoor::kHeaderSize + outdoor::kTagSize) || length > int(outdoor::kMaxWireSize) ||
        std::memcmp(info->src_addr, credentials::outdoorMac.data(), 6) ||
        std::memcmp(info->des_addr, credentials::indoorMac.data(), 6)) return;
    Received message{}; message.length = uint16_t(length);
    std::copy_n(data, size_t(length), message.bytes.begin());
    (void)xQueueSend(self->inbox_, &message, 0);
  }
  bool radioBegin() {
    uint32_t version = 0;
    if (esp_now_init() != ESP_OK || esp_now_get_version(&version) != ESP_OK || version < 2 ||
        esp_now_set_pmk(credentials::pmk.data()) != ESP_OK) return false;
    esp_now_peer_info_t peer{}; peer.encrypt = true; peer.ifidx = WIFI_IF_STA; peer.channel = 0;
    std::copy(credentials::outdoorMac.begin(), credentials::outdoorMac.end(), peer.peer_addr);
    std::copy(credentials::lmk.begin(), credentials::lmk.end(), peer.lmk);
    return esp_now_add_peer(&peer) == ESP_OK && esp_now_register_recv_cb(receive) == ESP_OK;
  }
  void pump() {
    session_.tick(millis());
    for (int budget = 0; budget < 4 && xQueueReceive(inbox_, &incoming_, 0) == pdTRUE; ++budget) {
      if (!codec_.decode(incoming_.bytes.data(), incoming_.length, false, frame_)) continue;
      if (!session_.bound() && !session_.waiting() && frame_.type == outdoor::Type::Hello) {
        outdoor::Nonce nonce{}; do { esp_fill_random(nonce.data(), nonce.size()); } while (!outdoor::nonzero(nonce));
        session_.configure(millis(), frame_, nonce, group_);
      } else session_.receive(millis(), frame_);
    }
    if (session_.transmit(millis(), outgoing_) && codec_.encode(outgoing_, true, wire_))
      (void)esp_now_send(credentials::outdoorMac.data(), wire_.bytes.data(), wire_.length);
    connected_ = session_.bound();
  }
  bool exchange(std::vector<uint8_t>& command, std::vector<uint8_t>& response) {
    if (!enabled_ || admission_ != attemptAdmission_ || !session_.exchange(millis(), command.data(), command.size())) {
      transportError_ = true; return false;
    }
    while (session_.waiting() && session_.active() && enabled_ && admission_ == attemptAdmission_ && WiFi.status() == WL_CONNECTED) {
      pump(); vTaskDelay(pdMS_TO_TICKS(2));
    }
    if (!session_.active() || !enabled_ || admission_ != attemptAdmission_ || !session_.takeResponse(response_)) {
      transportError_ = true; return false;
    }
    response.assign(response_.payload.begin(), response_.payload.begin() + response_.length); return true;
  }
  void authenticate() {
    const auto attempt = attempts_.start(millis());
    if (!attempt) { session_.release(millis()); return; }
    std::unique_lock<std::mutex> lock(store_.mutex, std::try_to_lock);
    if (!lock || !store_.provisioned() || !enabled_) {
      attempts_.complete(millis(), attempt, door::NfcAttemptLimiter::Verdict::Aborted);
      session_.release(millis()); return;
    }
    transportError_ = false; attemptAdmission_ = admission_;
    auto channel = std::make_shared<Channel>([this](auto& command, auto& response) { return exchange(command, response); });
    // SELECT and all cryptographic challenge/response validation run indoors.
    const std::vector<uint8_t> select{0x00,0xa4,0x04,0x00,0x07,0xa0,0x00,0x00,0x08,0x58,0x01,0x01,0x00};
    bool authorized = false;
    auto selected = channel->transceive(select);
    if (selected.ok()) {
      ddk::Session authSession(channel, store_);
      auto profile = ddk::homekey::make_profile();
      if (profile->validate_select(authSession, selected.data) == ddk::FailureReason::None) {
        auto state = ddk::FlowState::Selected;
        for (int budget = 0; budget < 8 && state != ddk::FlowState::Done && state != ddk::FlowState::Failed; ++budget)
          state = profile->step(authSession, state);
        const auto outcome = profile->finalize(authSession);
        authorized = state == ddk::FlowState::Done && bool(outcome) && outcome.issuer && outcome.endpoint && store_.healthy();
      }
    }
    const auto verdict = transportError_ || !store_.healthy() || !enabled_ || admission_ != attemptAdmission_ ? door::NfcAttemptLimiter::Verdict::Aborted :
        authorized ? door::NfcAttemptLimiter::Verdict::Authorized : door::NfcAttemptLimiter::Verdict::InvalidCredential;
    if (attempts_.complete(millis(), attempt, verdict) && session_.active()) {
      Verified result{millis(), store_.generation(), attemptAdmission_};
      (void)xQueueSend(verified_, &result, 0);
    }
    session_.release(millis());
  }
  static void workerEntry(void* object) { static_cast<HomeKeyManager*>(object)->worker(); }
  void worker() {
    bool radio = false;
    uint32_t pingAt = millis(), generation = std::numeric_limits<uint32_t>::max();
    while (true) {
      if (WiFi.status() != WL_CONNECTED) { session_.reset(); connected_ = false; vTaskDelay(pdMS_TO_TICKS(50)); continue; }
      if (!radio) { radio = radioBegin(); if (!radio) { configured_ = false; vTaskDelete(nullptr); return; } }
      {
        std::unique_lock<std::mutex> lock(store_.mutex, std::try_to_lock);
        if (!lock) { vTaskDelay(pdMS_TO_TICKS(5)); continue; }
        provisioned_ = store_.provisioned();
        if (!provisioned_) { session_.reset(); connected_ = false; }
        else if (store_.generation() != generation) {
          const auto& id = store_.reader_identity().group_identifier;
          std::array<uint8_t,8> group{}; std::copy(id.begin(), id.end(), group.begin());
          if (group != group_) { session_.reset(); group_ = group; }
          generation = store_.generation();
        }
      }
      // Let HAP provisioning acquire the store while no identity is available.
      // Never sleep holding the credential mutex.
      if (!provisioned_) { vTaskDelay(pdMS_TO_TICKS(50)); continue; }
      pump();
      if (session_.takeTag(millis())) {
        if (enabled_) authenticate(); else session_.release(millis());
      } else if (uint32_t(millis() - pingAt) >= 1000 && session_.ping(millis())) pingAt = millis();
      vTaskDelay(pdMS_TO_TICKS(5));
    }
  }
  inline static HomeKeyManager* instance_ = nullptr;
  HomeKeyStore& store_; door::NfcAttemptLimiter& attempts_;
  Hmac auth_; outdoor::Codec codec_; door::IndoorSession session_;
  QueueHandle_t inbox_ = nullptr, verified_ = nullptr;
  Received incoming_{}; outdoor::Frame frame_{}, outgoing_{}, response_{}; outdoor::Wire wire_{};
  std::array<uint8_t,8> group_{};
  std::atomic<bool> enabled_{false}, configured_{false}, provisioned_{false}, connected_{false};
  std::atomic<uint32_t> admission_{0};
  uint32_t attemptAdmission_ = 0;
  bool transportError_ = false;
};
