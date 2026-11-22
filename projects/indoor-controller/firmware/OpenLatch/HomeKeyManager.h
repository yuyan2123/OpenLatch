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
#include <LanTransport.h>
#include "HomeKeyStore.h"
#include "HomeKeyCrypto.h"
#include "NfcAttemptLimiter.h"
#include "RfidFobs.h"
#include "ddk/homekey/ProfileFactory.h"
#include "ddk/transport/NfcChannel.h"
#if defined(DOOR_NATIVE_NFC_TEST)
#ifdef ARDUINO
#error "Native NFC adapters cannot be built for a device"
#endif
#include <TestNfcPeer.h>
#elif __has_include("nfc_peer.h")
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
    fobs_ = xQueueCreate(1, sizeof(door::IndoorSession::Tag));
    if (!inbox_ || !verified_ || !fobs_) return false;
    instance_ = this;
    configured_ = true;
    if (xTaskCreate(workerEntry, "homekey", 14336, this, 1, nullptr) != pdPASS) { configured_ = false; return false; }
    return true;
  }
  void enabled(bool value) { if (enabled_.exchange(value) && !value) ++admission_; }
  // The loop supplies the list revision and current permission as one snapshot.
  void fobAdmission(uint32_t revision, bool accept) { fobAdmission_ = (revision << 1) | uint32_t(accept); }
  bool takeFob(door::RfidUid& uid) {
    door::IndoorSession::Tag tag;
    if (!fobs_ || xQueueReceive(fobs_, &tag, 0) != pdTRUE ||
        !(tag.admission & 1) || tag.admission != fobAdmission_ ||
        uint32_t(millis() - tag.at) >= kResultAgeMs || !connected() || WiFi.status() != WL_CONNECTED) return false;
    uid = {tag.uid, tag.length};
    return true;
  }
  bool configured() const { return configured_; }
  bool provisioned() const { return provisioned_; }
  bool connected() const { return connectedLink_ != outdoor::Link::None; }
  const char* transport() const {
    switch (connectedLink_.load()) {
      case outdoor::Link::Wifi: return "wifi";
      case outdoor::Link::EspNow: return "esp-now";
      default: return "";
    }
  }
  bool healthy() const { return store_.healthy() && (!credentials::provisioned || configured_); }
  // Main loop consumes each result once. Queued results expire, and any pause
  // or credential edit invalidates results produced before that change.
  bool takeVerified() {
    Verified result{};
    return verified_ && xQueueReceive(verified_, &result, 0) == pdTRUE && enabled_ &&
           result.admission == admission_ && result.generation == store_.generation() && store_.healthy() &&
           uint32_t(millis() - result.at) < kResultAgeMs && connected() && WiFi.status() == WL_CONNECTED;
  }
 private:
  static constexpr uint32_t kResultAgeMs = 250;
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
  void receiveFrame(const uint8_t* data, size_t length, outdoor::Link link, outdoor::LanPeer peer = {}) {
    if (!codec_.decode(data, length, false, frame_)) return;
    if (!session_.bound() && !session_.waiting()) {
      if (frame_.type != outdoor::Type::Hello) return;
      outdoor::Nonce nonce{}; do { esp_fill_random(nonce.data(), nonce.size()); } while (!outdoor::nonzero(nonce));
      if (session_.configure(millis(), frame_, nonce, group_)) { link_ = link; lanPeer_ = peer; }
      return;
    }
    if (link == link_ && (link != outdoor::Link::Wifi || peer == lanPeer_))
      session_.receive(millis(), frame_, fobAdmission_);
  }
  void pump() {
    const uint32_t now = millis();
    session_.tick(now);
    if (!lanListening_ && uint32_t(now - lanAttemptAt_) >= 1000) {
      lanAttemptAt_ = now;
      lanListening_ = lan_.begin();
    }
    outdoor::LanPeer peer;
    for (int budget = 0; budget < 4 && lan_.receive(wire_, peer); ++budget)
      receiveFrame(wire_.bytes.data(), wire_.length, outdoor::Link::Wifi, peer);
    for (int budget = 0; budget < 4 && xQueueReceive(inbox_, &incoming_, 0) == pdTRUE; ++budget)
      receiveFrame(incoming_.bytes.data(), incoming_.length, outdoor::Link::EspNow);
    if (session_.transmit(millis(), outgoing_) && codec_.encode(outgoing_, true, wire_)) {
      if (link_ == outdoor::Link::Wifi) (void)lan_.send(wire_, lanPeer_);
      else (void)esp_now_send(credentials::outdoorMac.data(), wire_.bytes.data(), wire_.length);
    }
    connectedLink_ = session_.bound() ? link_ : outdoor::Link::None;
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
    if (!attempt) {
      Serial.println("nfc_homekey: verification unavailable");
      session_.release(millis()); return;
    }
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
    } else if (transportError_) Serial.println("nfc_homekey: APDU exchange failed");
    else if (verdict == door::NfcAttemptLimiter::Verdict::InvalidCredential) Serial.println("nfc_homekey: credential rejected");
    session_.release(millis());
  }
 public:
  // Called only by the NFC task. One iteration is also driven by host adapter tests.
  uint32_t poll() {
    if (WiFi.status() != WL_CONNECTED) {
      if (lanListening_) { lan_.stop(); lanListening_ = false; }
      session_.reset(); connectedLink_ = outdoor::Link::None;
      return 50;
    }
    if (!radio_) {
      radio_ = radioBegin();
      if (!radio_) { configured_ = false; return 0; }
    }
    {
      std::unique_lock<std::mutex> lock(store_.mutex, std::try_to_lock);
      if (!lock) return 5;
      provisioned_ = store_.provisioned();
      if (!provisioned_ || store_.generation() != generation_) {
        // A zero group keeps the paired reader available before Apple Home setup.
        std::array<uint8_t,8> group{};
        if (provisioned_) {
          const auto& id = store_.reader_identity().group_identifier;
          std::copy(id.begin(), id.end(), group.begin());
        }
        if (group != group_) { session_.reset(); group_ = group; }
        generation_ = store_.generation();
      }
    }
    pump();
    // A previous Release may still be pending. A new Tag supersedes a Ping in
    // the session, so keepalive retries cannot delay NFC authentication.
    door::IndoorSession::Tag tag;
    if (!session_.waiting() && session_.takeTag(millis(), &tag)) {
      // Reserve ISO-DEP targets for the Home Key APDU path.
      if (tag.sak & 0x20) {
        if (provisioned_ && enabled_) authenticate();
        else session_.release(millis());
      } else {
        if ((tag.admission & 1) && tag.admission == fobAdmission_ && door::RfidUid{tag.uid, tag.length}.valid())
          (void)xQueueSend(fobs_, &tag, 0);
        session_.release(millis());
      }
    } else if (uint32_t(millis() - pingAt_) >= 1000 && session_.ping(millis())) pingAt_ = millis();
    return 5;
  }
 private:
  static void workerEntry(void* object) { static_cast<HomeKeyManager*>(object)->worker(); }
  void worker() {
    while (const uint32_t delayMs = poll()) vTaskDelay(pdMS_TO_TICKS(delayMs));
    vTaskDelete(nullptr);
  }
  inline static HomeKeyManager* instance_ = nullptr;
  HomeKeyStore& store_; door::NfcAttemptLimiter& attempts_;
  Hmac auth_; outdoor::Codec codec_; door::IndoorSession session_;
  outdoor::LanTransport lan_{credentials::hmacKey, true};
  outdoor::LanPeer lanPeer_{};
  outdoor::Link link_ = outdoor::Link::None;
  std::atomic<outdoor::Link> connectedLink_{outdoor::Link::None};
  uint32_t lanAttemptAt_ = uint32_t(0) - 1000;
  bool lanListening_ = false;
  QueueHandle_t inbox_ = nullptr, verified_ = nullptr, fobs_ = nullptr;
  Received incoming_{}; outdoor::Frame frame_{}, outgoing_{}, response_{}; outdoor::Wire wire_{};
  std::array<uint8_t,8> group_{};
  std::atomic<bool> enabled_{false}, configured_{false}, provisioned_{false};
  std::atomic<uint32_t> admission_{0};
  std::atomic<uint32_t> fobAdmission_{0};
  uint32_t attemptAdmission_ = 0;
  bool transportError_ = false, radio_ = false;
  uint32_t pingAt_ = 0, generation_ = std::numeric_limits<uint32_t>::max();
};
