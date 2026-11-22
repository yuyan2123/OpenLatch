#pragma once
#include <atomic>
#include <driver/gpio.h>
#include <esp_timer.h>

class TapLed {
 public:
  TapLed(gpio_num_t pin, bool activeHigh) : pin_(pin), activeHigh_(activeHigh) {}

  bool begin() {
    if (gpio_set_level(pin_, !activeHigh_) != ESP_OK || gpio_set_direction(pin_, GPIO_MODE_OUTPUT) != ESP_OK)
      return false;
    esp_timer_create_args_t args{};
    args.callback = finish;
    args.arg = this;
    args.name = "tap_led";
    return esp_timer_create(&args, &timer_) == ESP_OK;
  }

  bool setActiveHigh(bool activeHigh) {
    if (flashing_ || (timer_ && gpio_set_level(pin_, !activeHigh) != ESP_OK)) return false;
    activeHigh_ = activeHigh;
    return true;
  }

  bool activeHigh() const { return activeHigh_; }

  void flash() {
    if (!timer_ || flashing_.exchange(true)) return;
    gpio_set_level(pin_, activeHigh_);
    if (esp_timer_start_once(timer_, 150000) != ESP_OK) finish(this);
  }

 private:
  static void finish(void* argument) {
    auto& led = *static_cast<TapLed*>(argument);
    gpio_set_level(led.pin_, !led.activeHigh_);
    led.flashing_ = false;
  }

  gpio_num_t pin_;
  bool activeHigh_;
  esp_timer_handle_t timer_ = nullptr;
  std::atomic<bool> flashing_{false};
};
