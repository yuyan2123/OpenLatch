#pragma once
#include <WiFi.h>
#include "BoundedWebServer.h"

// A separate listener keeps the setup server's access window and routes independent.
class LanSubnetApi {
 public:
  void begin() {
    server_.on("/api/same-subnet", HTTP_GET, [this]() {
      server_.sendHeader("Cache-Control", "no-store");
      server_.send(200, "application/json", sameSubnet() ? "{\"same_subnet\":true}" : "{\"same_subnet\":false}");
    });
    server_.onNotFound([this]() { server_.send(404, "text/plain", ""); });
  }

  void poll() {
    if (WiFi.status() != WL_CONNECTED) {
      if (attempted_) server_.stop();
      attempted_ = false;
      return;
    }
    const uint32_t now = millis();
    if (!server_.listening() && (!attempted_ || uint32_t(now - attemptedAt_) >= 1000)) {
      attempted_ = true;
      attemptedAt_ = now;
      server_.begin();
    }
    if (server_.listening()) server_.handleClient();
  }

 private:
  bool sameSubnet() {
    if (WiFi.status() != WL_CONNECTED) return false;
    // The SDK converts IPv4-mapped socket addresses to IPv4 and non-IPv4 to zero on cast.
    const uint32_t station = WiFi.localIP();
    const uint32_t destination = server_.client().localIP();
    const uint32_t source = server_.client().remoteIP();
    const uint32_t mask = WiFi.subnetMask();
    return station && source && mask && destination == station && ((source ^ station) & mask) == 0;
  }

  BoundedWebServer server_{8080};
  uint32_t attemptedAt_ = 0;
  bool attempted_ = false;
};
