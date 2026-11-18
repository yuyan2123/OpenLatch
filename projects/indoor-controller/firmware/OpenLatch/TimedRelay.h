#pragma once
#include <Arduino.h>
#include <driver/gpio.h>
#include <esp_timer.h>
#include "Controller.h"

// Serializes GPIO and timer operations against Wi-Fi/timer task callbacks.
// esp_timer is independent of the Arduino/HomeSpan loop, but is a SOFTWARE
// deadline: OS stalls, brownouts and welded contacts need hardware validation.
class TimedRelay final : public door::Relay {
 public:
  bool begin() {
    if (gpio_set_level(pin_, 0) != ESP_OK) return false;
    gpio_config_t config{};
    config.pin_bit_mask = uint64_t(1) << pin_;
    config.mode = GPIO_MODE_OUTPUT;
    config.pull_up_en = GPIO_PULLUP_DISABLE;
    config.pull_down_en = GPIO_PULLDOWN_ENABLE;
    config.intr_type = GPIO_INTR_DISABLE;
    if (gpio_config(&config) != ESP_OK) return false;
    if (gpio_set_drive_capability(pin_, GPIO_DRIVE_CAP_3) != ESP_OK) return false;
    esp_timer_create_args_t args{};
    args.callback = timeout;
    args.arg = this;
    args.name = "relay_off";
    ready_ = esp_timer_create(&args, &timer_) == ESP_OK;
    return ready_;
  }
  bool start(uint32_t ms, door::Source source = door::Source::HomeKit) override {
    if (!ready_ || ms == 0 || ms > door::kMaxPulseMs) return false;
    // Main loop is the only caller of start. stop() from a network callback
    // can revoke permission before/after timer arming, including this window.
    esp_timer_stop(timer_);
    portENTER_CRITICAL(&mux_);
    if (!permitted(source)) { portEXIT_CRITICAL(&mux_); return false; }
    source_ = source;
    active_ = true;
    portEXIT_CRITICAL(&mux_);
    // Arm BEFORE HIGH. If scheduling delays us until after timeout, stop()
    // clears active_ and the guard below prevents a late, unbounded HIGH.
    if (esp_timer_start_once(timer_, uint64_t(ms) * 1000) != ESP_OK) {
      stop();
      return false;
    }
    portENTER_CRITICAL(&mux_);
    const bool armed = active_ && permitted(source);
    if (armed) gpio_set_level(pin_, 1);
    portEXIT_CRITICAL(&mux_);
    return armed;
  }
  void stop() override {
    portENTER_CRITICAL(&mux_);
    gpio_set_level(pin_, 0);
    active_ = false;
    portEXIT_CRITICAL(&mux_);
    // Do not cancel here: a queued old timeout must only turn the GPIO OFF.
    // start() removes the previous timer before any new HIGH transition.
  }
  void permit(bool value) {
    portENTER_CRITICAL(&mux_);
    permitted_ = value;
    if (!value && source_ == door::Source::HomeKit) {
      gpio_set_level(pin_, 0); active_ = false;
    }
    portEXIT_CRITICAL(&mux_);
  }
  void permitInfrared(bool value) {
    portENTER_CRITICAL(&mux_);
    infraredPermitted_ = value;
    if (!value && source_ == door::Source::Infrared) {
      gpio_set_level(pin_, 0); active_ = false;
    }
    portEXIT_CRITICAL(&mux_);
  }
  void permitNfc(bool value) {
    portENTER_CRITICAL(&mux_);
    nfcPermitted_ = value;
    if (!value && source_ == door::Source::Nfc) { gpio_set_level(pin_, 0); active_ = false; }
    portEXIT_CRITICAL(&mux_);
  }
  bool active() const override {
    portENTER_CRITICAL(&mux_);
    const bool value = active_;
    portEXIT_CRITICAL(&mux_);
    return value;
  }
 private:
  // Called only while holding mux_. Network revocation cannot grant local access.
  bool permitted(door::Source source) const {
    if (source == door::Source::Nfc) return nfcPermitted_;
    return source == door::Source::Infrared ? infraredPermitted_ : permitted_;
  }
  static void timeout(void* arg) { static_cast<TimedRelay*>(arg)->stop(); }
  static constexpr gpio_num_t pin_ = static_cast<gpio_num_t>(door::kRelayPin);
  mutable portMUX_TYPE mux_ = portMUX_INITIALIZER_UNLOCKED;
  esp_timer_handle_t timer_ = nullptr;
  bool ready_ = false, permitted_ = false, infraredPermitted_ = false, active_ = false;
  bool nfcPermitted_ = false;
  door::Source source_ = door::Source::HomeKit;
};
