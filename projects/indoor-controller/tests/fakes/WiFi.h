#pragma once
#include "Arduino.h"
constexpr int WL_CONNECTED = 3, WIFI_STA = 1, WIFI_AP_STA = 3;
enum wifi_ps_type_t { WIFI_PS_NONE, WIFI_PS_MIN_MODEM, WIFI_PS_MAX_MODEM };
struct IPAddress {
  String value;
  String toString() const { return value; }
  operator uint32_t() const {
    unsigned a, b, c, d;
    if (std::sscanf(value.c_str(), "%u.%u.%u.%u", &a, &b, &c, &d) != 4 ||
        (a | b | c | d) > 255) return 0;
    return a | b << 8 | c << 16 | d << 24;
  }
};
struct FakeWiFi {
  bool connected = false, ap = false;
  int attempts = 0, apStarts = 0, apStops = 0;
  String ssid, password, apPassword;
  IPAddress stationIp{"192.168.1.42"}, stationMask{"255.255.255.0"};
  wifi_ps_type_t sleep = WIFI_PS_MIN_MODEM;
  wifi_ps_type_t getSleep() const { return sleep; }
  bool setSleep(wifi_ps_type_t value) { sleep = value; return true; }
  int RSSI() const { return -55; }
  void persistent(bool) {}
  void setAutoReconnect(bool) {}
  void mode(int) {}
  int status() const { return connected ? WL_CONNECTED : 0; }
  void begin(const char* s, const char* p) { ssid = s; password = p; ++attempts; }
  bool softAP(const char*, const char* p) { ap = true; apPassword = p; ++apStarts; return true; }
  void softAPdisconnect(bool) { ap = false; ++apStops; }
  IPAddress localIP() const { return stationIp; }
  IPAddress subnetMask() const { return stationMask; }
  IPAddress softAPIP() const { return {"192.168.4.1"}; }
};
inline FakeWiFi WiFi;
