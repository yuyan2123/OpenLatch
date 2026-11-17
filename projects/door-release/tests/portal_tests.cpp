#include <cstdlib>
#include <iostream>
#include "../firmware/DoorRelease/SetupPortal.h"
#define CHECK(x) do { if (!(x)) { std::cerr << __func__ << ':' << __LINE__ << " failed: " #x "\n"; std::exit(1); } } while (0)
struct MemoryIr : door::IrStorage {
  door::IrKeys keys{};
  bool failSave = false;
  door::IrLoad load(door::IrKeys& out) override { out = keys; return keys.count() ? door::IrLoad::Loaded : door::IrLoad::Empty; }
  bool save(const door::IrKeys& input) override { if (failSave) return false; keys = input; return true; }
};
struct Fixture {
  door::DeviceSettings settings{};
  SettingsStore store;
  MemoryIr irStore, setupIrStore;
  door::Infrared ir{irStore};
  door::Infrared setupIr{setupIrStore};
  TimedRelay relay;
  door::Controller controller{relay};
  door::NfcAttemptLimiter nfcAttempts;
  SetupPortal portal{settings, store, ir, setupIr, controller, relay, nfcAttempts};
  Fixture(bool credentials = false) {
    fakeMillis = 0; WiFi = {};
    nvsRecord.clear(); nvsPending.clear(); nvsWrites = 0;
    nvsOpenFails = nvsReadFails = nvsWriteFails = nvsCommitFails = nvsCorruptReadback = false;
    std::strcpy(settings.pairingCode, "59263814"); std::strcpy(settings.setupPassword, door::kDefaultSetupPassword);
    if (credentials) { std::strcpy(settings.ssid, "Home"); std::strcpy(settings.wifiPassword, "secretpass"); }
    CHECK(store.load(settings) == SettingsStore::Load::Missing); CHECK(store.save(settings));
    CHECK(relay.begin()); controller.boot(0); CHECK(ir.begin(0)); CHECK(setupIr.begin(0));
    nfcAttempts.begin();
    portal.begin(0, true, true);
  }
  void token() {
    fakeWeb->request("/api/status"); CHECK(fakeWeb->code == 200);
    const std::string prefix = "\"token\":\"";
    const size_t start = fakeWeb->body.find(prefix) + prefix.size();
    CHECK(start >= prefix.size());
    fakeWeb->inputHeaders["X-Setup-Token"] = fakeWeb->body.substr(start, 32);
    fakeWeb->inputHeaders["Origin"] = "http://" + fakeWeb->host;
  }
};
void authentication_and_request_guards() {
  Fixture f; CHECK(WiFi.ap); CHECK(fakeWeb->active); CHECK(fakeWeb->port == 80);
  CHECK(WiFi.apPassword == door::kDefaultSetupPassword);
  fakeWeb->request("/"); CHECK(fakeWeb->code == 200); CHECK(!fakeWeb->authenticated);
  CHECK(fakeWeb->body.find("Add door button") != std::string::npos);
  fakeWeb->request("/api/status"); CHECK(fakeWeb->code == 200);
  CHECK(fakeWeb->body.find("\"relay_pulse_ms\":200") != std::string::npos);
  CHECK(fakeWeb->body.find("wifiPassword") == std::string::npos);
  CHECK(fakeWeb->body.find(door::kDefaultSetupPassword) == std::string::npos);
  CHECK(fakeWeb->outputHeaders["Cache-Control"] == "no-store");
  fakeWeb->request("/api/ir/learn", HTTP_POST); CHECK(fakeWeb->code == 403);
  for (const auto* route : {"/api/setup-ir/learn", "/api/setup-ir/cancel", "/api/setup-ir/forget", "/api/ir/remove", "/api/setup-ir/remove", "/api/activity"}) {
    fakeWeb->request(route, HTTP_POST); CHECK(fakeWeb->code == 403); CHECK(!f.setupIr.learning());
    fakeWeb->request(route, HTTP_GET); CHECK(fakeWeb->code == 302); CHECK(!f.setupIr.learning());
  }
  f.token(); fakeWeb->inputHeaders["Origin"] = "http://attacker.invalid";
  fakeWeb->request("/api/ir/learn", HTTP_POST); CHECK(fakeWeb->code == 403); CHECK(!f.ir.learning());
  fakeWeb->inputHeaders["Origin"] = "http://192.168.4.1";
  fakeWeb->length = 1025; fakeWeb->request("/api/ir/learn", HTTP_POST); CHECK(fakeWeb->code == 413);
  fakeWeb->length = 0;
  fakeWeb->host = "attacker.invalid"; fakeWeb->request("/api/ir/learn", HTTP_POST); CHECK(fakeWeb->code == 403);
  fakeWeb->request("/api/status"); CHECK(fakeWeb->code == 302);
  CHECK(fakeWeb->outputHeaders["Location"] == "http://192.168.4.1/");
  CHECK(!fakeGpio); CHECK(!f.ir.learning());
}
void browser_learning_and_forget() {
  Fixture f; f.token();
  fakeMillis = 3000; f.controller.connectivity(fakeMillis, true, true); f.relay.permit(true);
  CHECK(f.controller.homekit(fakeMillis, true, true) == door::Result::Accepted); CHECK(fakeGpio == 1);
  fakeWeb->request("/api/ir/learn", HTTP_POST); CHECK(fakeWeb->code == 200); CHECK(f.ir.learning()); CHECK(!fakeGpio);
  CHECK(f.controller.homekit(fakeMillis, true, true) == door::Result::Inhibited);
  const door::IrKey button{door::IrProtocol::Nec, 1, 2, 32};
  fakeMillis += 500; f.portal.receiveIr(fakeMillis, button);
  fakeWeb->request("/api/status"); CHECK(fakeWeb->body.find("First press captured") != std::string::npos);
  fakeMillis += 500; f.portal.receiveIr(fakeMillis, button);
  CHECK(f.ir.enabled()); CHECK(!fakeGpio);
  fakeWeb->request("/api/status"); CHECK(fakeWeb->body.find("Button learned and saved") != std::string::npos);
  fakeWeb->request("/api/ir/forget", HTTP_POST); CHECK(fakeWeb->code == 200); CHECK(!f.ir.enabled()); CHECK(!fakeGpio);
  fakeWeb->request("/api/ir/learn", HTTP_GET); CHECK(!f.ir.learning());
  fakeWeb->request("/api/ir/learn", HTTP_POST); CHECK(f.ir.learning());
  fakeWeb->request("/api/ir/cancel", HTTP_POST); CHECK(!f.ir.learning()); CHECK(!fakeGpio);
}
void expiry_and_late_offline_recovery() {
  Fixture f(true); CHECK(!WiFi.ap); CHECK(WiFi.attempts == 1);
  CHECK(WiFi.getSleep() == WIFI_PS_NONE);
  fakeWeb->host = "192.168.1.42"; WiFi.connected = true;
  f.portal.poll(); f.token();
  fakeMillis = 600000;
  fakeWeb->request("/api/ir/learn", HTTP_POST); CHECK(fakeWeb->code == 410); CHECK(!f.ir.learning());
  f.portal.poll(); CHECK(!fakeWeb->active);
  CHECK(WiFi.getSleep() == WIFI_PS_MIN_MODEM);
  WiFi.connected = false; f.portal.poll(); CHECK(!WiFi.ap);
  fakeMillis += 30000; f.portal.poll(); CHECK(WiFi.ap); CHECK(fakeWeb->active);
  CHECK(WiFi.getSleep() == WIFI_PS_NONE);
  fakeMillis += 3600000; f.portal.poll(); CHECK(WiFi.ap); CHECK(fakeWeb->active);
  fakeWeb->host = "192.168.4.1"; f.token();
  fakeWeb->request("/api/ir/learn", HTTP_POST); CHECK(f.ir.learning());
  // A successful user action while offline also starts a fresh idle window.
  WiFi.connected = true; f.portal.poll();
  CHECK(!WiFi.ap); CHECK(fakeWeb->active); CHECK(f.ir.learning());
  fakeMillis += door::kSetupWindowMs; f.portal.poll();
  CHECK(!WiFi.ap); CHECK(!fakeWeb->active); CHECK(!f.ir.learning());
  CHECK(WiFi.getSleep() == WIFI_PS_MIN_MODEM);
}
void save_validation_and_restart() {
  Fixture f; f.token();
  fakeWeb->request("/api/settings", HTTP_POST); CHECK(fakeWeb->code == 400); CHECK(!f.portal.restarting());
  fakeWeb->arguments = {{"ssid", "New Home"}, {"wifi_password", "newpassword"}, {"setup_password", ""}, {"relay_pulse_ms", "575"}};
  nvsWriteFails = true;
  fakeWeb->request("/api/settings", HTTP_POST); CHECK(fakeWeb->code == 500); CHECK(!f.portal.restarting());
  CHECK(!f.settings.ssid[0]);
  CHECK(f.settings.relayPulseMs == 200);
  nvsWriteFails = false; fakeMillis = 3000;
  fakeWeb->request("/api/settings", HTTP_POST); CHECK(fakeWeb->code == 200); CHECK(f.portal.restarting());
  CHECK(!f.portal.restartDue(3999)); CHECK(f.portal.restartDue(4000)); CHECK(!fakeGpio);
  SettingsStore reboot; door::DeviceSettings loaded;
  CHECK(reboot.load(loaded) == SettingsStore::Load::Loaded); CHECK(std::string(loaded.ssid) == "New Home");
  CHECK(std::string(loaded.wifiPassword) == "newpassword");
  CHECK(loaded.relayPulseMs == 575);
  // Apply the persisted duration through the same boot path as the firmware.
  f.controller.boot(fakeMillis, loaded.relayPulseMs);
  f.controller.connectivity(fakeMillis + 3000, true, true); f.relay.permit(true);
  CHECK(f.controller.homekit(fakeMillis + 3000, true, true) == door::Result::Accepted);
  CHECK(fakeTimer.duration == 575000); f.relay.stop();
  fakeWeb->request("/api/ir/learn", HTTP_POST); CHECK(fakeWeb->code == 503); CHECK(!f.ir.learning());
}
void relay_time_validation() {
  Fixture f; f.token();
  fakeWeb->arguments = {{"ssid", ""}, {"wifi_password", ""}, {"setup_password", ""}};
  const int writes = nvsWrites;
  for (const auto* value : {"", "0", "801", "-1", "1.5", "2e2", " 200", "4294967297"}) {
    fakeWeb->arguments["relay_pulse_ms"] = value;
    fakeWeb->request("/api/settings", HTTP_POST); CHECK(fakeWeb->code == 400);
    CHECK(!f.portal.restarting()); CHECK(!fakeGpio); CHECK(nvsWrites == writes);
  }
  fakeWeb->arguments["relay_pulse_ms"] = std::string("200\0junk", 8);
  fakeWeb->request("/api/settings", HTTP_POST); CHECK(fakeWeb->code == 400); CHECK(nvsWrites == writes);
  // Older clients omit the field and retain the current duration.
  f.settings.relayPulseMs = 650; fakeWeb->arguments.erase("relay_pulse_ms");
  fakeWeb->request("/api/settings", HTTP_POST); CHECK(fakeWeb->code == 200);
  SettingsStore reboot; door::DeviceSettings loaded;
  CHECK(reboot.load(loaded) == SettingsStore::Load::Loaded); CHECK(loaded.relayPulseMs == 650);
}
void nfc_limits_validation_and_status() {
  Fixture f; f.token();
  fakeWeb->arguments = {{"ssid", ""}, {"wifi_password", ""}, {"setup_password", ""},
                        {"nfc_max_failures", "3"}, {"nfc_lockout_seconds", "120"}};
  const int writes = nvsWrites;
  for (const auto* invalid : {"", "0", "21", "-1", "1.5", " 5", "999999999"}) {
    fakeWeb->arguments["nfc_max_failures"] = invalid;
    fakeWeb->request("/api/settings", HTTP_POST); CHECK(fakeWeb->code == 400);
    CHECK(!f.portal.restarting()); CHECK(nvsWrites == writes);
  }
  fakeWeb->arguments["nfc_max_failures"] = "3";
  for (const auto* invalid : {"", "0", "3601", "-1", "1.5", " 60", "999999999"}) {
    fakeWeb->arguments["nfc_lockout_seconds"] = invalid;
    fakeWeb->request("/api/settings", HTTP_POST); CHECK(fakeWeb->code == 400); CHECK(nvsWrites == writes);
  }
  fakeWeb->arguments["nfc_lockout_seconds"] = std::string("60\0bad", 6);
  fakeWeb->request("/api/settings", HTTP_POST); CHECK(fakeWeb->code == 400);
  fakeWeb->arguments.erase("nfc_lockout_seconds");
  fakeWeb->request("/api/settings", HTTP_POST); CHECK(fakeWeb->code == 400);
  f.nfcAttempts.begin(1, 60);
  const auto attempt = f.nfcAttempts.start(0);
  CHECK(!f.nfcAttempts.complete(0, attempt, door::NfcAttemptLimiter::Verdict::InvalidCredential));
  f.portal.nfcStatus(true, true, true);
  fakeWeb->request("/api/status"); CHECK(fakeWeb->code == 200);
  CHECK(fakeWeb->body.find("\"nfc_failures\":1") != std::string::npos);
  CHECK(fakeWeb->body.find("\"nfc_lockout_remaining_ms\":60000") != std::string::npos);
  CHECK(fakeWeb->body.find("\"nfc_connected\":true") != std::string::npos);
  // HTTP never accepts authentication outcomes or resets the limiter.
  fakeWeb->request("/api/nfc/unlock", HTTP_POST); CHECK(fakeWeb->code == 404); CHECK(!fakeGpio);
  fakeWeb->arguments["nfc_lockout_seconds"] = "120";
  fakeWeb->request("/api/settings", HTTP_POST); CHECK(fakeWeb->code == 200);
  SettingsStore reboot; door::DeviceSettings loaded;
  CHECK(reboot.load(loaded) == SettingsStore::Load::Loaded);
  CHECK(loaded.nfcMaxFailures == 3); CHECK(loaded.nfcLockoutSeconds == 120);
}
void setup_button_learning_and_reopening() {
  Fixture f(true); WiFi.connected = true; fakeWeb->host = "192.168.1.42";
  f.portal.poll(); f.token();
  const door::IrKey doorButton{door::IrProtocol::Nec, 1, 2, 32};
  const door::IrKey setupButton{door::IrProtocol::Nec, 1, 3, 32};
  fakeMillis = 3000;
  f.irStore.keys.entries[0] = doorButton; CHECK(f.ir.begin(fakeMillis));
  f.controller.connectivity(fakeMillis, true, true); f.relay.permit(true);
  fakeWeb->request("/api/setup-ir/learn", HTTP_POST); CHECK(fakeWeb->code == 200);
  CHECK(f.setupIr.learning()); CHECK(f.portal.irLearning());
  CHECK(f.controller.homekit(fakeMillis, true, true) == door::Result::Inhibited);
  fakeWeb->request("/api/ir/learn", HTTP_POST); CHECK(fakeWeb->code == 409);
  fakeWeb->request("/api/ir/cancel", HTTP_POST); CHECK(fakeWeb->code == 200);
  CHECK(f.controller.homekit(fakeMillis, true, true) == door::Result::Inhibited);
  fakeMillis += 500; CHECK(f.portal.receiveIr(fakeMillis, doorButton) == door::IrEvent::Ignored);
  fakeWeb->request("/api/status"); CHECK(fakeWeb->body.find("Choose a different button") != std::string::npos);
  fakeMillis += 500; f.portal.receiveIr(fakeMillis, setupButton); CHECK(f.setupIr.learning());
  fakeMillis += 500; CHECK(f.portal.receiveIr(fakeMillis, setupButton) == door::IrEvent::Ignored);
  CHECK(f.setupIr.enabled()); CHECK(f.setupIr.key() == setupButton); CHECK(!fakeGpio);
  door::Infrared reboot(f.setupIrStore); CHECK(reboot.begin(0)); CHECK(reboot.key() == setupButton);
  fakeMillis = 603000; f.portal.poll(); CHECK(!fakeWeb->active);
  // Unknown frames, repeats, invalid frames and held full frames cannot reopen.
  f.portal.receiveIr(fakeMillis, {door::IrProtocol::Nec, 1, 99, 32}); CHECK(!fakeWeb->active);
  fakeMillis += 500; f.portal.receiveIr(fakeMillis, setupButton, true); CHECK(!fakeWeb->active);
  fakeMillis += 500; f.portal.receiveIr(fakeMillis, setupButton, false, true); CHECK(!fakeWeb->active);
  fakeMillis += 100; f.portal.receiveIr(fakeMillis, setupButton); CHECK(!fakeWeb->active);
  fakeMillis += 500; CHECK(f.portal.receiveIr(fakeMillis, setupButton) == door::IrEvent::Ignored);
  CHECK(fakeWeb->active); CHECK(!fakeGpio); CHECK(f.ir.key() == doorButton);
  const uint32_t opened = fakeMillis;
  fakeWeb->request("/api/status"); CHECK(fakeWeb->body.find("\"remaining_seconds\":600") != std::string::npos);
  // Holding the button must not keep renewing the window.
  for (uint32_t t = opened + 100; t < opened + 1000; t += 100) f.portal.receiveIr(t, setupButton);
  fakeMillis = opened + 600000; f.portal.poll(); CHECK(!fakeWeb->active);
  // A released second press opens a fresh window without a reboot.
  f.portal.receiveIr(fakeMillis, setupButton); CHECK(fakeWeb->active); CHECK(!fakeGpio);
  f.token(); fakeWeb->request("/api/ir/learn", HTTP_POST); CHECK(fakeWeb->code == 200);
  fakeMillis += 500; CHECK(f.portal.receiveIr(fakeMillis, setupButton) == door::IrEvent::Ignored);
  CHECK(f.ir.learning()); CHECK(f.ir.key() == doorButton);
  fakeWeb->request("/api/ir/cancel", HTTP_POST);
  fakeWeb->request("/api/setup-ir/learn", HTTP_POST); CHECK(f.setupIr.learning());
  fakeWeb->request("/api/setup-ir/cancel", HTTP_POST); CHECK(!f.setupIr.learning());
  CHECK(f.setupIr.key() == setupButton);
  fakeWeb->request("/api/setup-ir/forget", HTTP_POST); CHECK(fakeWeb->code == 200);
  CHECK(!f.setupIr.enabled()); CHECK(f.ir.key() == doorButton);
  fakeMillis += 600000; f.portal.poll(); CHECK(!fakeWeb->active);
  f.portal.receiveIr(fakeMillis, setupButton); CHECK(!fakeWeb->active);
}
void activity_and_status_polling() {
  Fixture f(true); WiFi.connected = true; fakeWeb->host = "192.168.1.42";
  f.portal.poll(); f.token();
  fakeMillis = 590000;
  fakeWeb->request("/api/status"); CHECK(fakeWeb->body.find("\"remaining_seconds\":10") != std::string::npos);
  fakeWeb->request("/api/activity", HTTP_GET); CHECK(fakeWeb->code == 404);
  fakeWeb->inputHeaders["X-Setup-Token"] = "bad";
  fakeWeb->request("/api/activity", HTTP_POST); CHECK(fakeWeb->code == 403);
  fakeWeb->request("/api/status"); CHECK(fakeWeb->body.find("\"remaining_seconds\":10") != std::string::npos);
  f.token(); fakeWeb->request("/api/activity", HTTP_POST); CHECK(fakeWeb->code == 200);
  CHECK(fakeWeb->body.find("\"remaining_ms\":600000") != std::string::npos);
  fakeMillis = 600000; f.portal.poll(); CHECK(fakeWeb->active);
  fakeWeb->request("/api/status"); CHECK(fakeWeb->body.find("\"remaining_seconds\":590") != std::string::npos);
  CHECK(fakeWeb->body.find("\"remaining_ms\":590000") != std::string::npos);
  fakeMillis = 1190000;
  fakeWeb->request("/api/activity", HTTP_POST); CHECK(fakeWeb->code == 410);
  f.portal.poll(); CHECK(!fakeWeb->active);
}
void multiple_buttons_removal_and_conflicts() {
  Fixture f; f.token();
  const door::IrKey a{door::IrProtocol::Nec, 1, 2, 32}, b{door::IrProtocol::Nec, 1, 3, 32};
  const door::IrKey s1{door::IrProtocol::Nec, 1, 4, 32}, s2{door::IrProtocol::Nec, 1, 5, 32};
  f.irStore.keys.entries[0] = a; f.irStore.keys.entries[1] = b;
  f.setupIrStore.keys.entries[0] = s1; f.setupIrStore.keys.entries[1] = s2;
  CHECK(f.ir.begin(0)); CHECK(f.setupIr.begin(0));
  fakeWeb->request("/api/status"); CHECK(fakeWeb->body.find("\"ir_count\":2") != std::string::npos);
  CHECK(fakeWeb->body.find("\"setup_ir_count\":2") != std::string::npos);
  CHECK(fakeWeb->body.find("\"key\":\"NEC:1:3:32\"") != std::string::npos);
  fakeWeb->request("/api/ir/remove", HTTP_POST); CHECK(fakeWeb->code == 400); CHECK(f.ir.count() == 2);
  for (const auto* id : {"", "-1", "x", "100", "16"}) {
    fakeWeb->arguments = {{"id", id}, {"key", "NEC:1:2:32"}};
    fakeWeb->request("/api/ir/remove", HTTP_POST); CHECK(fakeWeb->code == 400 || fakeWeb->code == 404);
    CHECK(f.ir.count() == 2);
  }
  fakeWeb->arguments = {{"id", "1"}, {"key", "NEC:1:2:32"}};
  fakeWeb->request("/api/ir/remove", HTTP_POST); CHECK(fakeWeb->code == 409); CHECK(f.ir.count() == 2);
  fakeWeb->arguments["key"] = "NEC:1:3:32";
  fakeWeb->request("/api/ir/remove", HTTP_POST); CHECK(fakeWeb->code == 200);
  CHECK(f.ir.count() == 1); CHECK(f.ir.contains(a)); CHECK(!f.ir.contains(b)); CHECK(f.setupIr.count() == 2);
  fakeWeb->request("/api/ir/remove", HTTP_POST); CHECK(fakeWeb->code == 404);
  fakeMillis = 3000; fakeWeb->request("/api/ir/learn", HTTP_POST); CHECK(f.ir.learning());
  fakeMillis += 500; f.portal.receiveIr(fakeMillis, s2); CHECK(f.ir.learning());
  CHECK(f.ir.count() == 1);  // A setup button in slot 1 must also be excluded.
  fakeWeb->request("/api/setup-ir/remove", HTTP_POST); CHECK(fakeWeb->code == 409);
  fakeMillis += 500; f.portal.receiveIr(fakeMillis, b);
  fakeMillis += 500; CHECK(f.portal.receiveIr(fakeMillis, b) == door::IrEvent::Learned);
  CHECK(f.ir.count() == 2); CHECK(f.ir.contains(a)); CHECK(f.ir.contains(b)); CHECK(!fakeGpio);
  fakeWeb->request("/api/setup-ir/learn", HTTP_POST); CHECK(f.setupIr.learning());
  fakeMillis += 500; f.portal.receiveIr(fakeMillis, b); CHECK(f.setupIr.learning()); CHECK(f.setupIr.count() == 2);
  fakeWeb->request("/api/setup-ir/cancel", HTTP_POST);
  fakeWeb->arguments = {{"id", "0"}, {"key", "NEC:1:4:32"}};
  fakeWeb->request("/api/setup-ir/remove", HTTP_POST); CHECK(fakeWeb->code == 200);
  CHECK(f.setupIr.count() == 1); CHECK(f.setupIr.contains(s2)); CHECK(f.ir.count() == 2);
  f.irStore.failSave = true;
  fakeWeb->arguments = {{"id", "0"}, {"key", "NEC:1:2:32"}};
  fakeWeb->request("/api/ir/remove", HTTP_POST); CHECK(fakeWeb->code == 500);
  CHECK(!f.ir.ready()); CHECK(!fakeGpio); CHECK(f.setupIr.count() == 1);
}
void full_button_list_is_rejected() {
  Fixture f; f.token();
  for (uint16_t slot = 0; slot < door::kIrMaxKeys; ++slot) {
    f.irStore.keys.entries[slot] = {door::IrProtocol::Nec, 1, slot, 32};
    f.setupIrStore.keys.entries[slot] = {door::IrProtocol::Nec, 2, slot, 32};
  }
  CHECK(f.ir.begin(0)); CHECK(f.setupIr.begin(0));
  fakeWeb->request("/api/ir/learn", HTTP_POST); CHECK(fakeWeb->code == 409); CHECK(!f.ir.learning());
  fakeWeb->request("/api/setup-ir/learn", HTTP_POST); CHECK(fakeWeb->code == 409); CHECK(!f.setupIr.learning());
  CHECK(f.ir.count() == 16); CHECK(f.setupIr.count() == 16);
}
int main() {
  authentication_and_request_guards(); browser_learning_and_forget();
  expiry_and_late_offline_recovery(); save_validation_and_restart(); relay_time_validation(); nfc_limits_validation_and_status();
  setup_button_learning_and_reopening(); activity_and_status_polling();
  multiple_buttons_removal_and_conflicts(); full_button_list_is_rejected();
  std::cout << "PASS: no-login routes, CSRF guards, independent IR buttons, activity windows, AP recovery and save/restart.\n";
}
