#pragma once
#include "Arduino.h"
constexpr int WL_CONNECTED = 3, WIFI_STA = 1, WIFI_AP_STA = 3;
struct IPAddress {
  String value;
  String toString() const { return value; }
};
struct FakeWiFi {
  bool connected = false, ap = false;
  int attempts = 0, apStarts = 0, apStops = 0;
  String ssid, password, apPassword;
  void persistent(bool) {}
  void setAutoReconnect(bool) {}
  void mode(int) {}
  int status() const { return connected ? WL_CONNECTED : 0; }
  void begin(const char* s, const char* p) { ssid = s; password = p; ++attempts; }
  bool softAP(const char*, const char* p) { ap = true; apPassword = p; ++apStarts; return true; }
  void softAPdisconnect(bool) { ap = false; ++apStops; }
  IPAddress localIP() const { return {"192.168.1.42"}; }
  IPAddress softAPIP() const { return {"192.168.4.1"}; }
};
inline FakeWiFi WiFi;
