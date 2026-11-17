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
#include "EspFirmwareSink.h"
#include <functional>

class SetupPortal {
 public:
  SetupPortal(door::DeviceSettings& settings, SettingsStore& store, door::Infrared& infrared,
              door::Infrared& setupInfrared,
              door::Controller& controller, TimedRelay& relay, door::NfcAttemptLimiter& nfcAttempts)
      : settings_(settings), store_(store), infrared_(infrared), setupInfrared_(setupInfrared),
        controller_(controller), relay_(relay), nfcAttempts_(nfcAttempts) {}

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
    const char* headers[] = {"Origin", "X-Setup-Token", "X-OTA-Password", "Content-Type"};
    server_.collectHeaders(headers, 4);
    server_.on("/", HTTP_GET, [this]() {
      if (authorize()) server_.send_P(200, "text/html; charset=utf-8", kSetupPage);
    });
    server_.on("/api/status", HTTP_GET, [this]() { if (authorize()) status(); });
    server_.on("/api/activity", HTTP_POST, [this]() {
      if (authorize(true)) message(200, "Setup access extended.");
    });
    server_.on("/api/settings", HTTP_POST, [this]() { if (authorize(true)) saveSettings(); });
    server_.on("/api/firmware", HTTP_POST, [this]() { finishFirmware(); }, [this]() { receiveFirmware(); });
    server_.on("/api/ir/learn", HTTP_POST, [this]() { if (authorize(true)) learn(); });
    server_.on("/api/ir/remove", HTTP_POST, [this]() { if (authorize(true)) removeButton(false); });
    server_.on("/api/setup-ir/remove", HTTP_POST, [this]() { if (authorize(true)) removeButton(true); });
    server_.on("/api/ir/cancel", HTTP_POST, [this]() {
      if (!authorize(true)) return;
      infrared_.cancel(millis());
      controller_.inhibit(millis(), irLearning());
      lastIrEvent_ = "Learning cancelled. Saved buttons retained.";
      message(200, lastIrEvent_);
    });
    server_.on("/api/ir/forget", HTTP_POST, [this]() {
      if (!authorize(true)) return;
      if (setupInfrared_.learning()) { message(409, "Cancel setup button learning first."); return; }
      relay_.permitInfrared(false);
      const bool saved = infrared_.forget(millis());
      controller_.inhibit(millis(), false);
      lastIrEvent_ = saved ? "All door buttons cleared. Infrared release is disabled." :
                            "Erase failed. Infrared is disabled now; the stored button may return after reboot. Retry Forget button.";
      message(saved ? 200 : 500, lastIrEvent_);
    });
    server_.on("/api/setup-ir/learn", HTTP_POST, [this]() {
      if (!authorize(true)) return;
      if (setupInfrared_.count() >= door::kIrMaxKeys) { message(409, "Setup button list is full. Remove a button before adding another."); return; }
      if (irLearning() || !hardwareReady_ || controller_.faulted() || !setupInfrared_.learn(millis())) {
        message(409, "Learning is unavailable. Finish learning or check hardware and storage."); return;
      }
      controller_.inhibit(millis(), true);
      relay_.permitInfrared(false);
      lastSetupIrEvent_ = "Release for 500 ms, then press a new setup button twice within 30 seconds. Saved buttons are retained.";
      message(200, lastSetupIrEvent_);
    });
    server_.on("/api/setup-ir/cancel", HTTP_POST, [this]() {
      if (!authorize(true)) return;
      setupInfrared_.cancel(millis());
      controller_.inhibit(millis(), irLearning());
      lastSetupIrEvent_ = "Learning cancelled. Saved setup buttons retained.";
      message(200, lastSetupIrEvent_);
    });
    server_.on("/api/setup-ir/forget", HTTP_POST, [this]() {
      if (!authorize(true)) return;
      if (infrared_.learning()) { message(409, "Cancel door button learning first."); return; }
      const bool saved = setupInfrared_.forget(millis());
      controller_.inhibit(millis(), irLearning());
      lastSetupIrEvent_ = saved ? "All setup buttons cleared. Use the boot window to learn new buttons." :
                                 "Erase failed. Setup button disabled; retry before restarting.";
      message(saved ? 200 : 500, lastSetupIrEvent_);
    });
    server_.onNotFound([this]() {
      if (apActive_ && server_.method() == HTTP_GET) redirectToAp();
      else message(404, "Not found.");
    });
    pollNetwork();
  }

  void poll() {
    const uint32_t now = millis();
    nfcAttempts_.tick(now);
    firmware_.tick(now);
    if (infrared_.tick(now)) noteIrEvent(door::IrEvent::TimedOut);
    if (setupInfrared_.tick(now)) noteSetupIrEvent(door::IrEvent::TimedOut);
    pollNetwork();
    if (apActive_) dns_.processNextRequest();
    if (webActive_) server_.handleClient();
  }
  bool restarting() const { return restarting_; }
  bool updating() const { return firmware_.active(); }
  void beforeFirmwareUpdate(std::function<void()> callback) { beforeFirmware_ = std::move(callback); }
  void nfcStatus(bool configured, bool provisioned, bool connected) {
    nfcConfigured_ = configured; nfcProvisioned_ = provisioned; nfcConnected_ = connected;
  }
  bool restartDue(uint32_t now) const { return restarting_ && uint32_t(now - restartAt_) >= 1000; }
  const char* hostname() const { return hostname_; }
  bool irLearning() const { return infrared_.learning() || setupInfrared_.learning(); }
  // Feed every decoded frame to both filters so held buttons cannot rearm.
  // A setup frame is consumed here and can never request a relay pulse.
  door::IrEvent receiveIr(uint32_t now, const door::IrKey& input, bool repeat = false, bool invalid = false) {
    const bool doorLearning = infrared_.learning(), setupLearning = setupInfrared_.learning();
    const bool conflict = !invalid && !repeat && input.valid() &&
        ((doorLearning && setupInfrared_.contains(input)) || (setupLearning && infrared_.contains(input)));
    const auto setupEvent = setupInfrared_.receive(now, input, repeat, invalid || conflict || doorLearning || restarting_);
    const auto event = infrared_.receive(now, input, repeat, invalid || conflict || setupLearning || restarting_);
    noteIrEvent(event);
    noteSetupIrEvent(setupEvent);
    if (conflict) {
      if (setupLearning) lastSetupIrEvent_ = "This is the door button. Choose a different button for setup access.";
      else lastIrEvent_ = "This is the setup button. Choose a different button for door release.";
    }
    if (setupEvent == door::IrEvent::Matched && !doorLearning && !setupLearning && !restarting_) {
      policy_.openFromInfrared(now);
      pollNetwork();
      lastSetupIrEvent_ = "Setup access opened for 10 minutes. Reload the setup page.";
      Serial.println("setup_web: opened by infrared for ten minutes");
      return door::IrEvent::Ignored;
    }
    return event;
  }
  void noteIrEvent(door::IrEvent event) {
    switch (event) {
      case door::IrEvent::Captured: lastIrEvent_ = "First press captured. Release for 500 ms, then press the same button again."; break;
      case door::IrEvent::Mismatch: lastIrEvent_ = "Different button received. Press the first button again after releasing for 500 ms."; break;
      case door::IrEvent::Learned: lastIrEvent_ = "Button learned and saved. Existing buttons retained. Release, then press again to open."; break;
      case door::IrEvent::Duplicate: lastIrEvent_ = "This button is already saved. No duplicate added."; break;
      case door::IrEvent::StorageError: lastIrEvent_ = "Storage verification failed. Infrared is disabled. Use Clear all door buttons to repair."; break;
      case door::IrEvent::TimedOut: lastIrEvent_ = "Learning timed out. Saved buttons retained."; break;
      default: break;
    }
  }

 private:
  void noteSetupIrEvent(door::IrEvent event) {
    switch (event) {
      case door::IrEvent::Captured: lastSetupIrEvent_ = "First press captured. Release for 500 ms, then press the same button again."; break;
      case door::IrEvent::Mismatch: lastSetupIrEvent_ = "Different button received. Press the first button again after releasing for 500 ms."; break;
      case door::IrEvent::Learned: lastSetupIrEvent_ = "Setup button saved. Existing buttons retained. A later press opens setup access for 10 minutes."; break;
      case door::IrEvent::Duplicate: lastSetupIrEvent_ = "This setup button is already saved. No duplicate added."; break;
      case door::IrEvent::StorageError: lastSetupIrEvent_ = "Setup button storage failed. Use Clear all setup buttons, then learn again."; break;
      case door::IrEvent::TimedOut: lastSetupIrEvent_ = "Learning timed out. Saved setup buttons retained."; break;
      default: break;
    }
  }
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
      // Favor responsive HTTP while setup is open, then restore the prior
      // modem power policy when the window closes.
      savedSleep_ = WiFi.getSleep();
      WiFi.setSleep(WIFI_PS_NONE);
      server_.begin();
      webActive_ = true;
      Serial.println("setup_web: started on port 80");
    } else if (!wantWeb && webActive_) {
      server_.stop();
      webActive_ = false;
      WiFi.setSleep(savedSleep_);
      if (irLearning()) {
        infrared_.cancel(now);
        setupInfrared_.cancel(now);
        controller_.inhibit(now, restarting_);
        lastIrEvent_ = "Setup window closed. Learning cancelled; previous button retained.";
        lastSetupIrEvent_ = lastIrEvent_;
      }
      Serial.println("setup_web: closed; press the learned setup IR button or restart to reopen");
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
  bool authorize(bool mutation = false, bool firmware = false) {
    headers();
    const uint32_t now = millis();
    policy_.tick(now, WiFi.status() == WL_CONNECTED);
    if (!policy_.web(now)) { message(410, "Setup window closed. Press the learned setup button or restart, then reload."); return false; }
    if (!knownHost(server_.hostHeader())) {
      if (apActive_ && server_.method() == HTTP_GET) redirectToAp();
      else message(403, "Use the device IP address or its local hostname.");
      return false;
    }
    // Web login temporarily disabled at the owner's request. Keep for restoration.
    /*
    if (!server_.authenticate("admin", settings_.setupPassword)) {
      server_.requestAuthentication(DIGEST_AUTH, "DoorReleaseSetup", "Sign in with username admin and the setup password.");
      return false;
    }
    */
    if (restarting_) { message(503, "Device is restarting."); return false; }
    if (updating() && !firmware) { message(409, "Firmware update is in progress."); return false; }
    if (mutation) {
      const String origin = server_.header("Origin");
      if (server_.header("X-Setup-Token") != token_ ||
          (origin.length() && origin != String("http://") + server_.hostHeader())) {
        message(403, "Setup token or request origin is invalid. Reload this page.");
        return false;
      }
      if (!firmware && server_.clientContentLength() > 1024) {
        message(413, "Settings request is too large."); return false;
      }
    }
    return true;
  }
  void redirectToAp() {
    server_.sendHeader("Location", String("http://") + WiFi.softAPIP().toString() + "/");
    server_.send(302, "text/plain", "Open the device setup address in your browser.");
  }
  bool authorizeFirmware() {
    if (!authorize(true, true)) return false;
    const String password = server_.header("X-OTA-Password");
    const size_t expected = strlen(settings_.setupPassword);
    uint8_t difference = password.length() == expected ? 0 : 1;
    for (size_t i = 0; i < expected; ++i)
      difference |= uint8_t(settings_.setupPassword[i]) ^ (i < password.length() ? uint8_t(password[i]) : 0);
    if (difference) { message(403, "Incorrect setup password."); return false; }
    if (server_.header("Content-Type") != "application/octet-stream") {
      message(415, "Send one application .bin as application/octet-stream."); return false;
    }
    return true;
  }
  void receiveFirmware() {
    // WebServer uses the same callback for multipart and raw streams. Never
    // dereference raw() for a multipart request.
    if (server_.header("Content-Type") != "application/octet-stream") return;
    auto& upload = server_.raw();
    if (upload.status == RAW_START) {
      firmwareRequest_ = false;
      if (!authorizeFirmware()) { server_.client().stop(); return; }
      if (irLearning() || updating()) { message(409, "Finish button learning or the current update first."); server_.client().stop(); return; }
      const int length = server_.clientContentLength();
      if (!firmware_.capacity()) { message(409, "OTA partitions are unavailable. Install the OTA firmware over USB first."); server_.client().stop(); return; }
      if (length < 288 || size_t(length) > firmware_.capacity()) {
        message(413, "Firmware does not fit the OTA partition."); server_.client().stop(); return;
      }
      firmwareRequest_ = firmware_.start(millis(), size_t(length));
      if (!firmwareRequest_) { message(400, firmware_.error() ? firmware_.error() : "Firmware update is unavailable."); server_.client().stop(); return; }
      controller_.inhibit(millis(), true);
      relay_.permit(false); relay_.permitInfrared(false); relay_.permitNfc(false);
      if (beforeFirmware_) beforeFirmware_();
      policy_.noteActivity(millis());
    } else if (upload.status == RAW_WRITE && firmwareRequest_) {
      if (firmware_.active()) firmware_.write(millis(), upload.buf, upload.currentSize);
      policy_.noteActivity(millis());
    } else if (upload.status == RAW_ABORTED) {
      firmware_.cancel();
      controller_.inhibit(millis(), irLearning() || restarting_);
    }
  }
  void finishFirmware() {
    if (!authorizeFirmware()) return;
    if (!firmwareRequest_) { message(400, "No firmware image was received."); return; }
    firmwareRequest_ = false;
    if (!firmware_.finish(millis())) {
      controller_.inhibit(millis(), irLearning() || restarting_);
      message(400, firmware_.error() ? firmware_.error() : "Firmware update failed."); return;
    }
    restarting_ = true; restartAt_ = millis();
    message(200, "Firmware installed. Restarting now.");
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
    // Only accepted user actions renew access. GET/status and rejected requests do not.
    if (code >= 200 && code < 300 && server_.method() == HTTP_POST)
      policy_.noteActivity(millis());
    String json = String("{\"message\":") + quoted(text);
    if (code >= 200 && code < 300) {
      json += ",\"remaining_ms\":" + String(policy_.remaining(millis()));
      json += ",\"setup_window_ms\":" + String(door::kSetupWindowMs);
    }
    server_.send(code, "application/json", json + "}");
  }
  static String fingerprint(const door::IrKey& key) {
    return String(door::name(key.protocol)) + ":" + String(key.address) + ":" +
           String(key.command) + ":" + String(key.bits);
  }
  static String buttonList(const door::Infrared& ir) {
    String json = "[";
    bool first = true;
    for (uint8_t slot = 0; slot < door::kIrMaxKeys; ++slot) {
      const auto& key = ir.at(slot);
      if (!key.valid()) continue;
      if (!first) json += ",";
      first = false;
      json += String("{\"id\":") + String(slot) + ",\"protocol\":" + quoted(door::name(key.protocol));
      json += ",\"address\":" + String(key.address) + ",\"command\":" + String(key.command);
      json += ",\"bits\":" + String(key.bits) + ",\"key\":" + quoted(fingerprint(key)) + "}";
    }
    return json + "]";
  }
  void removeButton(bool setup) {
    if (irLearning()) { message(409, "Finish or cancel button learning before removing a button."); return; }
    const String id = server_.arg("id");
    if (!id.length() || id.length() > 2 || !server_.hasArg("key")) { message(400, "Missing or invalid button ID and key."); return; }
    uint8_t slot = 0;
    for (size_t i = 0; i < id.length(); ++i) {
      if (id[i] < '0' || id[i] > '9') { message(400, "Invalid button ID."); return; }
      slot = slot * 10 + uint8_t(id[i] - '0');
    }
    auto& ir = setup ? setupInfrared_ : infrared_;
    if (slot >= door::kIrMaxKeys || !ir.at(slot).valid()) { message(404, "Button no longer exists. Refresh the list."); return; }
    // Prevent a stale browser from removing a different button in a reused slot.
    if (server_.arg("key") != fingerprint(ir.at(slot))) { message(409, "Button changed. Refresh the list and try again."); return; }
    controller_.inhibit(millis(), true);
    relay_.permitInfrared(false);
    const bool saved = ir.remove(millis(), slot);
    controller_.inhibit(millis(), false);
    const char* result = saved ? "Button removed. Other saved buttons retained." :
        "Storage verification failed. This button list is disabled; clear it to repair before learning again.";
    if (setup) lastSetupIrEvent_ = result;
    else lastIrEvent_ = result;
    message(saved ? 200 : 500, result);
  }
  void status() {
    const bool connected = WiFi.status() == WL_CONNECTED;
    const auto& key = infrared_.key();
    String json;
    json.reserve(5200);
    json = String("{\"token\":") + quoted(token_) + ",\"ssid\":" + quoted(settings_.ssid);
    json += String(",\"connected\":") + (connected ? "true" : "false");
    json += ",\"uptime_ms\":" + String(millis());
    json += ",\"wifi_rssi\":" + String(connected ? WiFi.RSSI() : 0);
    json += String(",\"ap\":") + (apActive_ ? "true" : "false");
    json += ",\"ap_name\":" + quoted(apName_);
    json += ",\"ip\":" + quoted(connected ? WiFi.localIP().toString() : WiFi.softAPIP().toString());
    json += ",\"hostname\":" + quoted(String(hostname_) + ".local");
    const uint32_t remaining = policy_.remaining(millis());
    json += ",\"remaining_seconds\":" + String((remaining + 999) / 1000);
    json += ",\"remaining_ms\":" + String(remaining);
    json += ",\"setup_window_ms\":" + String(door::kSetupWindowMs);
    json += ",\"pairing_code\":" + quoted(settings_.pairingCode);
    json += ",\"relay_pulse_ms\":" + String(settings_.relayPulseMs);
    json += ",\"firmware_version\":" + quoted(door::kFirmwareVersion);
    json += ",\"ota_max_bytes\":" + String(firmware_.capacity());
    json += String(",\"ota_supported\":") + (firmware_.capacity() ? "true" : "false");
    json += String(",\"firmware_test_build\":") + (door::firmwareTestBuild() ? "true" : "false");
    json += ",\"nfc_max_failures\":" + String(settings_.nfcMaxFailures);
    json += ",\"nfc_lockout_seconds\":" + String(settings_.nfcLockoutSeconds);
    json += ",\"nfc_failures\":" + String(nfcAttempts_.failures(millis()));
    json += ",\"nfc_lockout_remaining_ms\":" + String(nfcAttempts_.remainingMs(millis()));
    json += String(",\"nfc_configured\":") + (nfcConfigured_ ? "true" : "false");
    json += String(",\"nfc_provisioned\":") + (nfcProvisioned_ ? "true" : "false");
    json += String(",\"nfc_connected\":") + (nfcConnected_ ? "true" : "false");
    json += String(",\"default_password\":") + (!strcmp(settings_.setupPassword, door::kDefaultSetupPassword) ? "true" : "false");
    json += String(",\"settings_ready\":") + (storageReady_ ? "true" : "false");
    json += String(",\"hardware_ready\":") + (hardwareReady_ && !controller_.faulted() ? "true" : "false");
    json += String(",\"ir_ready\":") + (infrared_.ready() ? "true" : "false");
    json += String(",\"ir_learning\":") + (infrared_.learning() ? "true" : "false");
    json += String(",\"ir_enabled\":") + (hardwareReady_ && infrared_.enabled() && !controller_.faulted() ? "true" : "false");
    json += ",\"ir_protocol\":" + quoted(door::name(key.protocol));
    json += ",\"ir_address\":" + String(key.address);
    json += ",\"ir_command\":" + String(key.command);
    json += ",\"ir_event\":" + quoted(lastIrEvent_);
    json += ",\"ir_count\":" + String(infrared_.count()) + ",\"ir_capacity\":" + String(door::kIrMaxKeys);
    json += ",\"ir_buttons\":" + buttonList(infrared_);
    const auto& setupKey = setupInfrared_.key();
    json += String(",\"setup_ir_ready\":") + (setupInfrared_.ready() ? "true" : "false");
    json += String(",\"setup_ir_learning\":") + (setupInfrared_.learning() ? "true" : "false");
    json += String(",\"setup_ir_enabled\":") + (setupInfrared_.enabled() ? "true" : "false");
    json += ",\"setup_ir_protocol\":" + quoted(door::name(setupKey.protocol));
    json += ",\"setup_ir_address\":" + String(setupKey.address);
    json += ",\"setup_ir_command\":" + String(setupKey.command);
    json += ",\"setup_ir_count\":" + String(setupInfrared_.count()) + ",\"setup_ir_capacity\":" + String(door::kIrMaxKeys);
    json += ",\"setup_ir_buttons\":" + buttonList(setupInfrared_);
    json += ",\"setup_ir_event\":" + quoted(lastSetupIrEvent_) + "}";
    server_.send(200, "application/json", json);
  }
  void learn() {
    if (infrared_.count() >= door::kIrMaxKeys) { message(409, "Door button list is full. Remove a button before adding another."); return; }
    if (irLearning() || !hardwareReady_ || controller_.faulted() || !infrared_.learn(millis())) {
      message(409, "Learning is unavailable. Check hardware and storage status."); return;
    }
    controller_.inhibit(millis(), true);
    relay_.permitInfrared(false);
    lastIrEvent_ = "Learning started. Release for 500 ms, then press the same button twice within 30 seconds. Existing buttons are retained.";
    message(200, lastIrEvent_);
  }
  void saveSettings() {
    if (irLearning()) { message(409, "Finish or cancel infrared learning before saving settings."); return; }
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
    if (server_.hasArg("relay_pulse_ms")) {
      const String pulse = server_.arg("relay_pulse_ms");
      if (pulse.length() != strlen(pulse.c_str())) { message(400, "Invalid relay on time."); return; }
      error = door::updateRelayPulse(candidate, pulse.c_str());
      if (error) { message(400, error); return; }
    }
    if (server_.hasArg("nfc_max_failures") || server_.hasArg("nfc_lockout_seconds")) {
      if (!server_.hasArg("nfc_max_failures") || !server_.hasArg("nfc_lockout_seconds")) {
        message(400, "Provide both RFID limit fields."); return;
      }
      const String failures = server_.arg("nfc_max_failures"), seconds = server_.arg("nfc_lockout_seconds");
      if (failures.length() != strlen(failures.c_str()) || seconds.length() != strlen(seconds.c_str())) {
        message(400, "RFID limits contain an invalid null byte."); return;
      }
      error = door::updateNfcLimits(candidate, failures.c_str(), seconds.c_str());
      if (error) { message(400, error); return; }
    }
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
  door::Infrared& setupInfrared_;
  door::Controller& controller_;
  TimedRelay& relay_;
  door::NfcAttemptLimiter& nfcAttempts_;
  WebServer server_{80};
  EspFirmwareSink firmwareSink_;
  door::FirmwareUpdate firmware_{firmwareSink_};
  std::function<void()> beforeFirmware_;
  bool firmwareRequest_ = false;
  DNSServer dns_;
  door::SetupPolicy policy_;
  char hostname_[40]{}, apName_[32]{}, token_[33]{};
  const char* lastIrEvent_ = "Ready. Start learning to register an unlock button.";
  const char* lastSetupIrEvent_ = "Not set. Learn a setup button here during the boot window.";
  bool webActive_ = false, apActive_ = false, storageReady_ = false, hardwareReady_ = false;
  bool restarting_ = false;
  bool nfcConfigured_ = false, nfcProvisioned_ = false, nfcConnected_ = false;
  wifi_ps_type_t savedSleep_ = WIFI_PS_MIN_MODEM;
  uint32_t restartAt_ = 0;
};
