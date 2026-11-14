#include <cstdlib>
#include <iostream>
#include "../firmware/DoorRelease/SetupPortal.h"
#define CHECK(x) do { if (!(x)) { std::cerr << __func__ << ':' << __LINE__ << " failed: " #x "\n"; std::exit(1); } } while (0)
struct MemoryIr : door::IrStorage {
  door::IrKey key{};
  door::IrLoad load(door::IrKey& out) override { out = key; return key.valid() ? door::IrLoad::Loaded : door::IrLoad::Empty; }
  bool save(const door::IrKey& input) override { key = input; return true; }
};
struct Fixture {
  door::DeviceSettings settings{};
  SettingsStore store;
  MemoryIr irStore, setupIrStore;
  door::Infrared ir{irStore};
  door::Infrared setupIr{setupIrStore};
  TimedRelay relay;
  door::Controller controller{relay};
  SetupPortal portal{settings, store, ir, setupIr, controller, relay};
  Fixture(bool credentials = false) {
    fakeMillis = 0; WiFi = {};
    nvsRecord.clear(); nvsPending.clear(); nvsWrites = 0;
    nvsOpenFails = nvsReadFails = nvsWriteFails = nvsCommitFails = nvsCorruptReadback = false;
    std::strcpy(settings.pairingCode, "59263814"); std::strcpy(settings.setupPassword, door::kDefaultSetupPassword);
    if (credentials) { std::strcpy(settings.ssid, "Home"); std::strcpy(settings.wifiPassword, "secretpass"); }
    CHECK(store.load(settings) == SettingsStore::Load::Missing); CHECK(store.save(settings));
    CHECK(relay.begin()); controller.boot(0); CHECK(ir.begin(0)); CHECK(setupIr.begin(0));
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
  CHECK(fakeWeb->body.find("Learn button") != std::string::npos);
  fakeWeb->request("/api/status"); CHECK(fakeWeb->code == 200);
  CHECK(fakeWeb->body.find("wifiPassword") == std::string::npos);
  CHECK(fakeWeb->body.find(door::kDefaultSetupPassword) == std::string::npos);
  CHECK(fakeWeb->outputHeaders["Cache-Control"] == "no-store");
  fakeWeb->request("/api/ir/learn", HTTP_POST); CHECK(fakeWeb->code == 403);
  for (const auto* route : {"/api/setup-ir/learn", "/api/setup-ir/cancel", "/api/setup-ir/forget", "/api/activity"}) {
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
  fakeWeb->host = "192.168.1.42"; WiFi.connected = true;
  f.portal.poll(); f.token();
  fakeMillis = 600000;
  fakeWeb->request("/api/ir/learn", HTTP_POST); CHECK(fakeWeb->code == 410); CHECK(!f.ir.learning());
  f.portal.poll(); CHECK(!fakeWeb->active);
  WiFi.connected = false; f.portal.poll(); CHECK(!WiFi.ap);
  fakeMillis += 30000; f.portal.poll(); CHECK(WiFi.ap); CHECK(fakeWeb->active);
  fakeMillis += 3600000; f.portal.poll(); CHECK(WiFi.ap); CHECK(fakeWeb->active);
  fakeWeb->host = "192.168.4.1"; f.token();
  fakeWeb->request("/api/ir/learn", HTTP_POST); CHECK(f.ir.learning());
  // A successful user action while offline also starts a fresh idle window.
  WiFi.connected = true; f.portal.poll();
  CHECK(!WiFi.ap); CHECK(fakeWeb->active); CHECK(f.ir.learning());
  fakeMillis += door::kSetupWindowMs; f.portal.poll();
  CHECK(!WiFi.ap); CHECK(!fakeWeb->active); CHECK(!f.ir.learning());
}
void save_validation_and_restart() {
  Fixture f; f.token();
  fakeWeb->request("/api/settings", HTTP_POST); CHECK(fakeWeb->code == 400); CHECK(!f.portal.restarting());
  fakeWeb->arguments = {{"ssid", "New Home"}, {"wifi_password", "newpassword"}, {"setup_password", ""}};
  nvsWriteFails = true;
  fakeWeb->request("/api/settings", HTTP_POST); CHECK(fakeWeb->code == 500); CHECK(!f.portal.restarting());
  CHECK(!f.settings.ssid[0]);
  nvsWriteFails = false; fakeMillis = 3000;
  fakeWeb->request("/api/settings", HTTP_POST); CHECK(fakeWeb->code == 200); CHECK(f.portal.restarting());
  CHECK(!f.portal.restartDue(3999)); CHECK(f.portal.restartDue(4000)); CHECK(!fakeGpio);
  SettingsStore reboot; door::DeviceSettings loaded;
  CHECK(reboot.load(loaded) == SettingsStore::Load::Loaded); CHECK(std::string(loaded.ssid) == "New Home");
  CHECK(std::string(loaded.wifiPassword) == "newpassword");
  fakeWeb->request("/api/ir/learn", HTTP_POST); CHECK(fakeWeb->code == 503); CHECK(!f.ir.learning());
}
void setup_button_learning_and_reopening() {
  Fixture f(true); WiFi.connected = true; fakeWeb->host = "192.168.1.42";
  f.portal.poll(); f.token();
  const door::IrKey doorButton{door::IrProtocol::Nec, 1, 2, 32};
  const door::IrKey setupButton{door::IrProtocol::Nec, 1, 3, 32};
  fakeMillis = 3000;
  f.irStore.key = doorButton; CHECK(f.ir.begin(fakeMillis));
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
  fakeMillis = 600000; f.portal.poll(); CHECK(fakeWeb->active);
  fakeWeb->request("/api/status"); CHECK(fakeWeb->body.find("\"remaining_seconds\":590") != std::string::npos);
  fakeMillis = 1190000;
  fakeWeb->request("/api/activity", HTTP_POST); CHECK(fakeWeb->code == 410);
  f.portal.poll(); CHECK(!fakeWeb->active);
}
int main() {
  authentication_and_request_guards(); browser_learning_and_forget();
  expiry_and_late_offline_recovery(); save_validation_and_restart();
  setup_button_learning_and_reopening(); activity_and_status_polling();
  std::cout << "PASS: no-login routes, CSRF guards, independent IR buttons, activity windows, AP recovery and save/restart.\n";
}
