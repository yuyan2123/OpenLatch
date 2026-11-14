#pragma once
#include <cstdint>
#include "esp_err.h"
using gpio_num_t = int;
constexpr int GPIO_MODE_OUTPUT = 1;
constexpr int GPIO_PULLUP_DISABLE = 0, GPIO_PULLDOWN_ENABLE = 1;
constexpr int GPIO_INTR_DISABLE = 0, GPIO_DRIVE_CAP_3 = 3;
struct gpio_config_t {
  uint64_t pin_bit_mask = 0;
  int mode = 0, pull_up_en = 0, pull_down_en = 0, intr_type = 0;
};
inline gpio_config_t fakeGpioConfig;
inline bool fakeGpioMuxSelected = false, fakeGpioOutput = false;
inline bool fakeGpioConfigFails = false, fakeGpioLevelFails = false, fakeGpioDriveFails = false;
inline int fakeGpioLatch = 0, fakeGpioDrive = 0;
inline int fakeGpio = 0;
inline int fakeHighWrites = 0;
inline void fakeUpdatePad() {
  if (fakeGpioMuxSelected && fakeGpioOutput) {
    fakeGpio = fakeGpioLatch;
    if (fakeGpio) ++fakeHighWrites;
  }
}
inline int gpio_set_level(gpio_num_t, int level) {
  if (fakeGpioLevelFails) return ESP_FAIL;
  fakeGpioLatch = level;
  fakeUpdatePad();
  return ESP_OK;
}
inline int gpio_set_direction(gpio_num_t, int mode) {
  fakeGpioOutput = mode == GPIO_MODE_OUTPUT;
  fakeUpdatePad();  // Does not select the GPIO function in IO_MUX.
  return ESP_OK;
}
inline int gpio_config(const gpio_config_t* config) {
  if (fakeGpioConfigFails) return ESP_FAIL;
  fakeGpioConfig = *config;
  fakeGpioMuxSelected = true;
  fakeGpioOutput = config->mode == GPIO_MODE_OUTPUT;
  fakeUpdatePad();
  return ESP_OK;
}
inline int gpio_set_drive_capability(gpio_num_t, int strength) {
  if (fakeGpioDriveFails) return ESP_FAIL;
  fakeGpioDrive = strength;
  return ESP_OK;
}
