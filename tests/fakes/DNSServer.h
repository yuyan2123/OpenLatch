#pragma once
#include "WiFi.h"
struct DNSServer {
  bool active = false;
  bool start(int, const char*, IPAddress) { active = true; return true; }
  void stop() { active = false; }
  void processNextRequest() {}
};
