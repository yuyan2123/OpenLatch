#pragma once
#include <cstdint>
#include "esp_err.h"
struct esp_timer_create_args_t {
  void (*callback)(void*) = nullptr;
  void* arg = nullptr;
  const char* name = nullptr;
};
struct FakeTimer {
  esp_timer_create_args_t args;
  bool armed = false;
  uint64_t duration = 0;
};
using esp_timer_handle_t = FakeTimer*;
inline FakeTimer fakeTimer;
inline bool fakeCreateFails = false, fakeStartFails = false;
inline void (*fakeAfterArm)() = nullptr;
inline int esp_timer_create(const esp_timer_create_args_t* args, esp_timer_handle_t* timer) {
  if (fakeCreateFails) return -1;
  fakeTimer = {*args, false, 0};
  *timer = &fakeTimer;
  return ESP_OK;
}
inline int esp_timer_stop(esp_timer_handle_t timer) { timer->armed = false; return ESP_OK; }
inline int esp_timer_start_once(esp_timer_handle_t timer, uint64_t us) {
  if (fakeStartFails) return -1;
  timer->armed = true;
  timer->duration = us;
  if (fakeAfterArm) fakeAfterArm();
  return ESP_OK;
}
inline void fakeExpire() {
  fakeTimer.armed = false;
  fakeTimer.args.callback(fakeTimer.args.arg);
}
