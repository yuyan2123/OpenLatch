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
  MemoryIr irStore;
  door::Infrared ir{irStore};
  TimedRelay relay;
  door::Controller controller{relay};
  SetupPortal portal{settings, store, ir, controller, relay};
  Fixture(bool credentials = false) {
    fakeMillis = 0; WiFi = {};
    nvsRecord.clear(); nvsPending.clear(); nvsWrites = 0;
    nvsOpenFails = nvsReadFails = nvsWriteFails = nvsCommitFails = nvsCorruptReadback = false;
    std::strcpy(settings.pairingCode, "59263814"); std::strcpy(settings.setupPassword, door::kDefaultSetupPassword);
    if (credentials) { std::strcpy(settings.ssid, "Home"); std::strcpy(settings.wifiPassword, "secretpass"); }
    CHECK(store.load(settings) == SettingsStore::Load::Missing); CHECK(store.save(settings));
    CHECK(relay.begin()); controller.boot(0); CHECK(ir.begin(0));
    portal.begin(0, true, true);
  }
  void login() { fakeWeb->authenticated = true; fakeWeb->suppliedPassword = settings.setupPassword; }
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
  fakeWeb->request("/"); CHECK(fakeWeb->code == 401); CHECK(fakeWeb->challenge == DIGEST_AUTH);
  fakeWeb->request("/api/ir/learn", HTTP_POST); CHECK(fakeWeb->code == 401); CHECK(!f.ir.learning());
  f.login(); fakeWeb->request("/"); CHECK(fakeWeb->code == 200); CHECK(fakeWeb->body.find("Learn button") != std::string::npos);
  fakeWeb->request("/api/status"); CHECK(fakeWeb->code == 200);
  CHECK(fakeWeb->body.find("wifiPassword") == std::string::npos);
  CHECK(fakeWeb->body.find(door::kDefaultSetupPassword) == std::string::npos);
  CHECK(fakeWeb->outputHeaders["Cache-Control"] == "no-store");
  fakeWeb->request("/api/ir/learn", HTTP_POST); CHECK(fakeWeb->code == 403);
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
  Fixture f; f.login(); f.token();
  fakeMillis = 3000; f.controller.connectivity(fakeMillis, true, true); f.relay.permit(true);
  CHECK(f.controller.homekit(fakeMillis, true, true) == door::Result::Accepted); CHECK(fakeGpio == 1);
  fakeWeb->request("/api/ir/learn", HTTP_POST); CHECK(fakeWeb->code == 200); CHECK(f.ir.learning()); CHECK(!fakeGpio);
  CHECK(f.controller.homekit(fakeMillis, true, true) == door::Result::Inhibited);
  const door::IrKey button{door::IrProtocol::Nec, 1, 2, 32};
  fakeMillis += 500; f.portal.noteIrEvent(f.ir.receive(fakeMillis, button));
  fakeWeb->request("/api/status"); CHECK(fakeWeb->body.find("First press captured") != std::string::npos);
  fakeMillis += 500; f.portal.noteIrEvent(f.ir.receive(fakeMillis, button));
  CHECK(f.ir.enabled()); CHECK(!fakeGpio);
  fakeWeb->request("/api/status"); CHECK(fakeWeb->body.find("Button learned and saved") != std::string::npos);
  fakeWeb->request("/api/ir/forget", HTTP_POST); CHECK(fakeWeb->code == 200); CHECK(!f.ir.enabled()); CHECK(!fakeGpio);
  fakeWeb->request("/api/ir/learn", HTTP_GET); CHECK(!f.ir.learning());
  fakeWeb->request("/api/ir/learn", HTTP_POST); CHECK(f.ir.learning());
  fakeWeb->request("/api/ir/cancel", HTTP_POST); CHECK(!f.ir.learning()); CHECK(!fakeGpio);
}
void expiry_and_late_offline_recovery() {
  Fixture f(true); CHECK(!WiFi.ap); CHECK(WiFi.attempts == 1);
  f.login(); fakeWeb->host = "192.168.1.42"; WiFi.connected = true;
  f.portal.poll(); f.token();
  fakeMillis = 600000;
  fakeWeb->request("/api/ir/learn", HTTP_POST); CHECK(fakeWeb->code == 410); CHECK(!f.ir.learning());
  f.portal.poll(); CHECK(!fakeWeb->active);
  WiFi.connected = false; f.portal.poll(); CHECK(!WiFi.ap);
  fakeMillis += 30000; f.portal.poll(); CHECK(WiFi.ap); CHECK(fakeWeb->active);
  fakeMillis += 3600000; f.portal.poll(); CHECK(WiFi.ap); CHECK(fakeWeb->active);
  fakeWeb->host = "192.168.4.1"; f.token();
  fakeWeb->request("/api/ir/learn", HTTP_POST); CHECK(f.ir.learning());
  WiFi.connected = true; f.portal.poll();
  CHECK(!WiFi.ap); CHECK(!fakeWeb->active); CHECK(!f.ir.learning());
}
void save_validation_and_restart() {
  Fixture f; f.login(); f.token();
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
int main() {
  authentication_and_request_guards(); browser_learning_and_forget();
  expiry_and_late_offline_recovery(); save_validation_and_restart();
  std::cout << "PASS: actual portal routes, authentication/CSRF guards, learning, AP lifecycle and save/restart.\n";
}
