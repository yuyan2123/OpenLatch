#pragma once
#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include <DNSServer.h>
#include <esp_system.h>
#include "SettingsStore.h"
#include "SetupPolicy.h"
#include "SetupPage.h"
#include "Infrared.h"
#include "TimedRelay.h"

class SetupPortal {
 public:
  SetupPortal(door::DeviceSettings& settings, SettingsStore& store, door::Infrared& infrared,
              door::Controller& controller, TimedRelay& relay)
      : settings_(settings), store_(store), infrared_(infrared), controller_(controller), relay_(relay) {}

  void begin(uint32_t bootAt, bool storageReady, bool hardwareReady) {
    storageReady_ = storageReady;
    hardwareReady_ = hardwareReady;
    const uint32_t suffix = uint32_t(ESP.getEfuseMac() >> 24) & 0xffffff;
    snprintf(hostname_, sizeof(hostname_), "door-release-%06lx", static_cast<unsigned long>(suffix));
    snprintf(apName_, sizeof(apName_), "DoorRelease-%06lX", static_cast<unsigned long>(suffix));
    for (int i = 0; i < 4; ++i) snprintf(token_ + i * 8, 9, "%08lx", static_cast<unsigned long>(esp_random()));
    policy_.begin(bootAt, settings_.ssid[0] != 0);
    WiFi.persistent(false);
    WiFi.setAutoReconnect(false);
    const char* headers[] = {"Origin", "X-Setup-Token"};
    server_.collectHeaders(headers, 2);
    server_.on("/", HTTP_GET, [this]() {
      if (authorize()) server_.send_P(200, "text/html; charset=utf-8", kSetupPage);
    });
    server_.on("/api/status", HTTP_GET, [this]() { if (authorize()) status(); });
    server_.on("/api/settings", HTTP_POST, [this]() { if (authorize(true)) saveSettings(); });
    server_.on("/api/ir/learn", HTTP_POST, [this]() { if (authorize(true)) learn(); });
    server_.on("/api/ir/cancel", HTTP_POST, [this]() {
      if (!authorize(true)) return;
      infrared_.cancel(millis());
      controller_.inhibit(millis(), false);
      lastIrEvent_ = "Learning cancelled. Previous button retained.";
      message(200, lastIrEvent_);
    });
    server_.on("/api/ir/forget", HTTP_POST, [this]() {
      if (!authorize(true)) return;
      relay_.permitInfrared(false);
      const bool saved = infrared_.forget(millis());
      controller_.inhibit(millis(), false);
      lastIrEvent_ = saved ? "Button forgotten. Infrared release is disabled." :
                            "Erase failed. Infrared is disabled now; the stored button may return after reboot. Retry Forget button.";
      message(saved ? 200 : 500, lastIrEvent_);
    });
    server_.onNotFound([this]() {
      if (apActive_ && server_.method() == HTTP_GET) redirectToAp();
      else message(404, "Not found.");
    });
    pollNetwork();
  }

  void poll() {
    pollNetwork();
    if (apActive_) dns_.processNextRequest();
    if (webActive_) server_.handleClient();
  }
  bool restarting() const { return restarting_; }
  bool restartDue(uint32_t now) const { return restarting_ && uint32_t(now - restartAt_) >= 1000; }
  const char* hostname() const { return hostname_; }
  void noteIrEvent(door::IrEvent event) {
    switch (event) {
      case door::IrEvent::Captured: lastIrEvent_ = "First press captured. Release for 500 ms, then press the same button again."; break;
      case door::IrEvent::Mismatch: lastIrEvent_ = "Different button received. Press the first button again after releasing for 500 ms."; break;
      case door::IrEvent::Learned: lastIrEvent_ = "Button learned and saved. Release, then press again to open."; break;
      case door::IrEvent::StorageError: lastIrEvent_ = "Storage verification failed. Infrared is disabled. Use Forget button to repair."; break;
      case door::IrEvent::TimedOut: lastIrEvent_ = "Learning timed out. Previous button retained."; break;
      default: break;
    }
  }

 private:
  void pollNetwork() {
    const uint32_t now = millis();
    policy_.tick(now, WiFi.status() == WL_CONNECTED);
    if (!restarting_ && policy_.stationAttempt(now)) {
      if (!apActive_) WiFi.mode(WIFI_STA);
      WiFi.begin(settings_.ssid, settings_.wifiPassword);
    }
    const bool wantAp = policy_.accessPoint(now);
    if (wantAp && !apActive_) {
      WiFi.mode(WIFI_AP_STA);
      if (WiFi.softAP(apName_, settings_.setupPassword)) {
        apActive_ = true;
        dns_.start(53, "*", WiFi.softAPIP());
        Serial.printf("setup_ap: %s; url=http://%s/\n", apName_, WiFi.softAPIP().toString().c_str());
      }
    } else if (!wantAp && apActive_) {
      dns_.stop();
      WiFi.softAPdisconnect(true);
      apActive_ = false;
    }
    const bool wantWeb = policy_.web(now);
    if (wantWeb && !webActive_) {
      server_.begin();
      webActive_ = true;
      Serial.println("setup_web: started on port 80");
    } else if (!wantWeb && webActive_) {
      server_.stop();
      webActive_ = false;
      if (infrared_.learning()) {
        infrared_.cancel(now);
        controller_.inhibit(now, restarting_);
        lastIrEvent_ = "Setup window closed. Learning cancelled; previous button retained.";
      }
      Serial.println("setup_web: closed; restart to reopen the ten-minute setup window");
    }
  }
  void headers() {
    server_.sendHeader("Cache-Control", "no-store");
    server_.sendHeader("X-Content-Type-Options", "nosniff");
    server_.sendHeader("X-Frame-Options", "DENY");
    server_.sendHeader("Referrer-Policy", "no-referrer");
    server_.sendHeader("Content-Security-Policy", "default-src 'none'; style-src 'unsafe-inline'; script-src 'unsafe-inline'; connect-src 'self'; form-action 'self'; base-uri 'none'; frame-ancestors 'none'");
  }
  bool knownHost(const String& host) const {
    String value = host;
    value.toLowerCase();
    if (value.endsWith(":80")) value.remove(value.length() - 3);
    if (value == String(hostname_) + ".local" || value == hostname_) return true;
    if (WiFi.status() == WL_CONNECTED && value == WiFi.localIP().toString()) return true;
    return apActive_ && value == WiFi.softAPIP().toString();
  }
  bool authorize(bool mutation = false) {
    headers();
    const uint32_t now = millis();
    policy_.tick(now, WiFi.status() == WL_CONNECTED);
    if (!policy_.web(now)) { message(410, "Setup window closed. Restart the device to configure again."); return false; }
    if (!knownHost(server_.hostHeader())) {
      if (apActive_ && server_.method() == HTTP_GET) redirectToAp();
      else message(403, "Use the device IP address or its local hostname.");
      return false;
    }
    if (!server_.authenticate("admin", settings_.setupPassword)) {
      server_.requestAuthentication(DIGEST_AUTH, "DoorReleaseSetup", "Sign in with username admin and the setup password.");
      return false;
    }
    if (restarting_) { message(503, "Settings saved. Device is restarting."); return false; }
    if (mutation) {
      const String origin = server_.header("Origin");
      if (server_.header("X-Setup-Token") != token_ ||
          (origin.length() && origin != String("http://") + server_.hostHeader())) {
        message(403, "Setup token or request origin is invalid. Reload this page.");
        return false;
      }
      if (server_.clientContentLength() > 1024) {
        message(413, "Settings request is too large."); return false;
      }
    }
    return true;
  }
  void redirectToAp() {
    server_.sendHeader("Location", String("http://") + WiFi.softAPIP().toString() + "/");
    server_.send(302, "text/plain", "Open the device setup address in your browser.");
  }
  static String quoted(const String& value) {
    String result = "\"";
    for (size_t i = 0; i < value.length(); ++i) {
      const uint8_t c = uint8_t(value[i]);
      if (c == '"' || c == '\\') { result += '\\'; result += char(c); }
      else if (c < 32) {
        char escape[7]; snprintf(escape, sizeof(escape), "\\u%04x", c); result += escape;
      } else result += char(c);
    }
    return result + '"';
  }
  void message(int code, const char* text) {
    server_.send( code, "application/json", String("{\"message\":") + quoted(text) + "}");
  }
  void status() {
    const bool connected = WiFi.status() == WL_CONNECTED;
    const auto& key = infrared_.key();
    String json;
    json.reserve(1000);
    json = String("{\"token\":") + quoted(token_) + ",\"ssid\":" + quoted(settings_.ssid);
    json += String(",\"connected\":") + (connected ? "true" : "false");
    json += String(",\"ap\":") + (apActive_ ? "true" : "false");
    json += ",\"ap_name\":" + quoted(apName_);
    json += ",\"ip\":" + quoted(connected ? WiFi.localIP().toString() : WiFi.softAPIP().toString());
    json += ",\"hostname\":" + quoted(String(hostname_) + ".local");
    json += ",\"remaining_seconds\":" + String((policy_.remaining(millis()) + 999) / 1000);
    json += ",\"pairing_code\":" + quoted(settings_.pairingCode);
    json += String(",\"default_password\":") + (!strcmp(settings_.setupPassword, door::kDefaultSetupPassword) ? "true" : "false");
    json += String(",\"settings_ready\":") + (storageReady_ ? "true" : "false");
    json += String(",\"hardware_ready\":") + (hardwareReady_ && !controller_.faulted() ? "true" : "false");
    json += String(",\"ir_ready\":") + (infrared_.ready() ? "true" : "false");
    json += String(",\"ir_learning\":") + (infrared_.learning() ? "true" : "false");
    json += String(",\"ir_enabled\":") + (hardwareReady_ && infrared_.enabled() && !controller_.faulted() ? "true" : "false");
    json += ",\"ir_protocol\":" + quoted(door::name(key.protocol));
    json += ",\"ir_address\":" + String(key.address);
    json += ",\"ir_command\":" + String(key.command);
    json += ",\"ir_event\":" + quoted(lastIrEvent_) + "}";
    server_.send(200, "application/json", json);
  }
  void learn() {
    if (!hardwareReady_ || controller_.faulted() || !infrared_.learn(millis())) {
      message(409, "Learning is unavailable. Check hardware and storage status."); return;
    }
    controller_.inhibit(millis(), true);
    relay_.permitInfrared(false);
    lastIrEvent_ = "Learning started. Release for 500 ms, then press the same button twice within 30 seconds.";
    message(200, lastIrEvent_);
  }
  void saveSettings() {
    if (infrared_.learning()) { message(409, "Finish or cancel infrared learning before saving settings."); return; }
    if (!server_.hasArg("ssid") || !server_.hasArg("wifi_password") || !server_.hasArg("setup_password")) {
      message(400, "Missing settings fields."); return;
    }
    const String ssid = server_.arg("ssid");
    const String wifiPassword = server_.arg("wifi_password");
    const String setupPassword = server_.arg("setup_password");
    if (ssid.length() != strlen(ssid.c_str()) || wifiPassword.length() != strlen(wifiPassword.c_str()) ||
        setupPassword.length() != strlen(setupPassword.c_str())) {
      message(400, "Settings contain an invalid null byte."); return;
    }
    door::DeviceSettings candidate = settings_;
    const char* error = door::updateSettings(candidate, ssid.c_str(), wifiPassword.c_str(),
                                            server_.arg("open_network") == "1", setupPassword.c_str());
    if (error) { message(400, error); return; }
    controller_.inhibit(millis(), true);
    relay_.permit(false);
    relay_.permitInfrared(false);
    if (!store_.save(candidate)) {
      controller_.inhibit(millis(), false);
      message(500, "Settings verification failed. Runtime settings are unchanged; retry before restarting."); return;
    }
    storageReady_ = true;
    restarting_ = true;
    restartAt_ = millis();
    message(200, "Settings saved. Restarting now. Reconnect to your Wi-Fi, or to the setup AP if connection fails.");
  }

  door::DeviceSettings& settings_;
  SettingsStore& store_;
  door::Infrared& infrared_;
  door::Controller& controller_;
  TimedRelay& relay_;
  WebServer server_{80};
  DNSServer dns_;
  door::SetupPolicy policy_;
  char hostname_[40]{}, apName_[32]{}, token_[33]{};
  const char* lastIrEvent_ = "Ready. Start learning to register an unlock button.";
  bool webActive_ = false, apActive_ = false, storageReady_ = false, hardwareReady_ = false;
  bool restarting_ = false;
  uint32_t restartAt_ = 0;
};
