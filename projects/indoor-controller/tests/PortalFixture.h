#pragma once
#include <cstdlib>
#include <iostream>
#include "../firmware/OpenLatch/SetupPortal.h"
#define CHECK(x) do { if (!(x)) { std::cerr << __func__ << ':' << __LINE__ << " failed: " #x "\n"; std::exit(1); } } while (0)
struct MemoryIr : door::IrStorage {
  door::IrKeys keys{};
  std::array<door::CredentialName, door::kIrMaxKeys> labels{};
  bool failSave = false;
  door::IrLoad load(door::IrKeys& out) override { out = keys; return keys.count() ? door::IrLoad::Loaded : door::IrLoad::Empty; }
  bool save(const door::IrKeys& input) override { if (failSave) return false; keys = input; return true; }
  door::CredentialName displayName(uint8_t slot, const door::IrKey&) const override { return labels[slot]; }
  bool rename(uint8_t slot, const door::IrKey&, const door::CredentialName& label) override {
    if (failSave) return false;
    labels[slot] = label;
    return true;
  }
  void clearName(uint8_t slot) override { labels[slot] = {}; }
};
struct Fixture {
  door::DeviceSettings settings{};
  SettingsStore store;
  MemoryIr irStore, setupIrStore;
  door::Infrared ir{irStore};
  door::Infrared setupIr{setupIrStore};
  door::RfidFobs fobs;
  TimedRelay relay;
  door::Controller controller{relay};
  door::NfcAttemptLimiter nfcAttempts;
  SetupPortal portal{settings, store, ir, setupIr, fobs, controller, relay, nfcAttempts};
  Fixture(bool credentials = false) {
    fakeMillis = 0; WiFi = {};
    resetOta();
    nvsRecord.clear(); nvsPending.clear(); nvsWrites = 0;
    nvsFobRecord.clear(); nvsFobPending.clear();
    nvsLabels.clear(); nvsPendingLabels.clear();
    nvsOpenFails = nvsReadFails = nvsWriteFails = nvsCommitFails = nvsCorruptReadback = false;
    std::strcpy(settings.pairingCode, "59263814"); std::strcpy(settings.setupPassword, door::kDefaultSetupPassword);
    if (credentials) { std::strcpy(settings.ssid, "Home"); std::strcpy(settings.wifiPassword, "secretpass"); }
    CHECK(store.load(settings) == SettingsStore::Load::Missing); CHECK(store.save(settings));
    CHECK(relay.begin()); controller.boot(0); CHECK(ir.begin(0)); CHECK(setupIr.begin(0)); CHECK(fobs.begin());
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
