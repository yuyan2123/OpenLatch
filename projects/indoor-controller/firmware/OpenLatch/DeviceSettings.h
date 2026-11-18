#pragma once
#include <cstddef>
#include <cstdint>
#include <cstring>
#include "Controller.h"
#include "NfcAttemptLimiter.h"

namespace door {
constexpr const char* kDefaultSetupPassword = "DoorSetup-2026";
struct DeviceSettings {
  char ssid[33]{};
  char wifiPassword[65]{};
  char pairingCode[9]{};
  char setupPassword[64]{};
  uint32_t relayPulseMs = kPulseMs;
  uint32_t nfcMaxFailures = kNfcMaxFailures;
  uint32_t nfcLockoutSeconds = kNfcLockoutSeconds;
  bool passwordRequired = false;
};
inline bool validPairingCode(const char* code) {
  if (std::strlen(code) != 8) return false;
  bool same = true;
  for (int i = 0; i < 8; ++i) {
    if (code[i] < '0' || code[i] > '9') return false;
    if (code[i] != code[0]) same = false;
  }
  return !same && std::strcmp(code, "46637726") && std::strcmp(code, "12345678") &&
         std::strcmp(code, "87654321");
}
inline bool printable(const char* value, size_t minimum, size_t maximum) {
  const size_t size = std::strlen(value);
  if (size < minimum || size > maximum) return false;
  for (size_t i = 0; i < size; ++i)
    if (static_cast<unsigned char>(value[i]) < 32 || static_cast<unsigned char>(value[i]) > 126) return false;
  return true;
}
inline bool validWifiPassword(const char* value) {
  const size_t size = std::strlen(value);
  if (!size) return true;  // Explicitly selected open networks are supported.
  if (size == 64) {
    for (size_t i = 0; i < size; ++i) {
      const char c = value[i];
      if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F'))) return false;
    }
    return true;
  }
  return printable(value, 8, 63);
}
inline bool validSettings(const DeviceSettings& settings) {
  if (!std::memchr(settings.ssid, 0, sizeof(settings.ssid)) ||
      !std::memchr(settings.wifiPassword, 0, sizeof(settings.wifiPassword)) ||
      !std::memchr(settings.pairingCode, 0, sizeof(settings.pairingCode)) ||
      !std::memchr(settings.setupPassword, 0, sizeof(settings.setupPassword))) return false;
  for (const char* c = settings.ssid; *c; ++c)
    if (static_cast<unsigned char>(*c) < 32 || *c == 127) return false;
  return settings.relayPulseMs > 0 && settings.relayPulseMs <= kMaxPulseMs &&
         validNfcLimits(settings.nfcMaxFailures, settings.nfcLockoutSeconds) &&
         validWifiPassword(settings.wifiPassword) && validPairingCode(settings.pairingCode) &&
         printable(settings.setupPassword, 12, 63) &&
         (settings.ssid[0] || !settings.wifiPassword[0]);
}
inline const char* updateRelayPulse(DeviceSettings& settings, const char* value) {
  const size_t length = std::strlen(value);
  if (!length || length > 3) return "Relay on time must be a whole number from 1 to 800 ms.";
  uint32_t ms = 0;
  for (size_t i = 0; i < length; ++i) {
    if (value[i] < '0' || value[i] > '9') return "Relay on time must be a whole number from 1 to 800 ms.";
    ms = ms * 10 + uint32_t(value[i] - '0');
  }
  if (!ms || ms > kMaxPulseMs) return "Relay on time must be a whole number from 1 to 800 ms.";
  settings.relayPulseMs = ms;
  return nullptr;
}
inline bool parseNfcLimit(const char* value, uint32_t maximum, uint32_t& output) {
  const size_t length = std::strlen(value);
  if (!length || length > 4) return false;
  uint32_t number = 0;
  for (size_t i = 0; i < length; ++i) {
    if (value[i] < '0' || value[i] > '9') return false;
    number = number * 10 + uint32_t(value[i] - '0');
  }
  if (!number || number > maximum) return false;
  output = number;
  return true;
}
inline const char* updateNfcLimits(DeviceSettings& settings, const char* failures, const char* seconds) {
  uint32_t count = 0, duration = 0;
  if (!parseNfcLimit(failures, 20, count)) return "Failed attempt limit must be a whole number from 1 to 20.";
  if (!parseNfcLimit(seconds, 3600, duration)) return "Lockout time must be a whole number from 1 to 3600 seconds.";
  settings.nfcMaxFailures = count;
  settings.nfcLockoutSeconds = duration;
  return nullptr;
}
// A single validator serves the HTTP form and desktop tests. Empty passwords
// retain existing values only when the SSID is unchanged and open=false.
inline const char* updateSettings(DeviceSettings& settings, const char* ssid,
                                  const char* wifiPassword, bool open,
                                  const char* setupPassword) {
  if (std::strlen(ssid) > 32) return "SSID must be at most 32 bytes.";
  if (std::strlen(wifiPassword) > 64) return "Wi-Fi password is too long.";
  if (std::strlen(setupPassword) > 63) return "Setup password is too long.";
  DeviceSettings candidate = settings;
  const bool changed = std::strcmp(ssid, settings.ssid) != 0;
  std::strcpy(candidate.ssid, ssid);
  if (open || !ssid[0]) candidate.wifiPassword[0] = 0;
  else if (wifiPassword[0]) std::strcpy(candidate.wifiPassword, wifiPassword);
  else if (changed) return "Enter a Wi-Fi password or explicitly select an open network.";
  if (setupPassword[0]) std::strcpy(candidate.setupPassword, setupPassword);
  if (!validSettings(candidate))
    return "Check SSID, Wi-Fi password (8-63 characters or 64 hex digits), and setup password (12-63 characters).";
  settings = candidate;
  return nullptr;
}
}  // namespace door
