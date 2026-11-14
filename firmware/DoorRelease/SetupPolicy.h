#pragma once
#include <cstdint>

namespace door {
constexpr uint32_t kSetupWindowMs = 10 * 60 * 1000;
constexpr uint32_t kWifiAttemptMs = 30000;

// Offline recovery remains available beyond the normal ten-minute window.
// Boot and a matched setup IR button open a timed window. Explicit user
// activity extends an open window; status polling never extends it.
class SetupPolicy {
 public:
  void begin(uint32_t now, bool credentials) {
    bootAt_ = offlineAt_ = attemptAt_ = now;
    credentials_ = credentials;
    online_ = false;
    expired_ = false;
    firstAttempt_ = credentials;
    recovery_ = !credentials;
  }
  void tick(uint32_t now, bool online) {
    if (!expired_ && uint32_t(now - bootAt_) >= kSetupWindowMs) expired_ = true;
    if (online_ && !online) offlineAt_ = now;
    online_ = online;
    if (online) recovery_ = false;
    else if (!credentials_ || uint32_t(now - offlineAt_) >= kWifiAttemptMs) recovery_ = true;
  }
  void openFromInfrared(uint32_t now) {
    bootAt_ = now;
    expired_ = false;
  }
  void noteActivity(uint32_t now) {
    if (web(now)) openFromInfrared(now);
  }
  bool accessPoint(uint32_t) const {
    return !online_ && recovery_;
  }
  bool web(uint32_t now) const { return remaining(now) > 0 || accessPoint(now); }
  bool stationAttempt(uint32_t now) {
    if (!credentials_ || online_) return false;
    if (!firstAttempt_ && uint32_t(now - attemptAt_) < kWifiAttemptMs) return false;
    firstAttempt_ = false;
    attemptAt_ = now;
    return true;
  }
  uint32_t remaining(uint32_t now) const {
    const uint32_t elapsed = uint32_t(now - bootAt_);
    return expired_ || elapsed >= kSetupWindowMs ? 0 : kSetupWindowMs - elapsed;
  }

 private:
  uint32_t bootAt_ = 0, offlineAt_ = 0, attemptAt_ = 0;
  bool credentials_ = false, online_ = false, expired_ = false, firstAttempt_ = false;
  bool recovery_ = false;
};
}  // namespace door
