#include <cstdlib>
#include <iostream>
#include <string>
#include "../firmware/DoorRelease/SetupPolicy.h"
#include "../firmware/DoorRelease/SettingsStore.h"
#define CHECK(x) do { if (!(x)) { std::cerr << __func__ << ':' << __LINE__ << " failed: " #x "\n"; std::exit(1); } } while (0)

void setup_window_and_recovery() {
  door::SetupPolicy policy;
  policy.begin(0, true);
  CHECK(policy.web(0)); CHECK(!policy.accessPoint(0));
  CHECK(policy.stationAttempt(0)); CHECK(!policy.stationAttempt(1));
  policy.tick(29999, false); CHECK(!policy.accessPoint(29999));
  policy.tick(30000, false); CHECK(policy.accessPoint(30000)); CHECK(policy.stationAttempt(30000));
  policy.tick(35000, true); CHECK(!policy.accessPoint(35000)); CHECK(policy.web(35000));
  CHECK(!policy.stationAttempt(35000));
  policy.tick(599999, true); CHECK(policy.web(599999)); CHECK(policy.remaining(599999) == 1);
  // Handlers must reject late requests even before the next policy tick.
  CHECK(!policy.web(600000));
  policy.tick(600000, true); CHECK(!policy.web(600000)); CHECK(policy.remaining(600000) == 0);
  policy.tick(700000, false); CHECK(!policy.web(700000));
  policy.tick(729999, false); CHECK(!policy.accessPoint(729999));
  policy.tick(730000, false); CHECK(policy.accessPoint(730000)); CHECK(policy.web(730000));
  policy.tick(7200000, false); CHECK(policy.accessPoint(7200000)); CHECK(policy.web(7200000));
  policy.tick(10, false); CHECK(policy.accessPoint(10)); CHECK(policy.web(10));  // Offline through a full timer wrap.
  policy.tick(7200001, true); CHECK(!policy.accessPoint(7200001)); CHECK(!policy.web(7200001));
  // Once the normal setup window has expired it cannot reopen at millis wrap.
  policy.tick(100, true); CHECK(!policy.web(100));
}
void first_boot_and_rollover() {
  door::SetupPolicy policy;
  const uint32_t start = 0xfffffff0u;
  policy.begin(start, false);
  CHECK(policy.accessPoint(start)); CHECK(policy.web(start)); CHECK(!policy.stationAttempt(start));
  policy.tick(start + 600000, false); CHECK(policy.web(start + 600000));
  CHECK(policy.remaining(start + 600000) == 0);
  policy.tick(start + 600001, true); CHECK(!policy.web(start + 600001));
  policy.begin(start, true);
  CHECK(policy.stationAttempt(start));
  CHECK(!policy.stationAttempt(start + 29999)); CHECK(policy.stationAttempt(start + 30000));
  policy.tick(start + 30000, false); CHECK(policy.accessPoint(start + 30000));
  policy.tick(start + 600000, true); CHECK(!policy.web(start + 600000));
}
void infrared_and_user_activity_windows() {
  door::SetupPolicy policy;
  policy.begin(0, true); policy.tick(0, true);
  policy.noteActivity(599999);
  CHECK(policy.remaining(600000) == 599999);
  policy.tick(1199999, true); CHECK(!policy.web(1199999));
  policy.noteActivity(1200000); CHECK(!policy.web(1200000));
  policy.openFromInfrared(1500000); CHECK(policy.remaining(1500000) == 600000);
  CHECK(policy.web(2099999)); CHECK(!policy.web(2100000));
  policy.tick(2100000, true); CHECK(!policy.web(2100000));
  const uint32_t start = 0xfffffff0u;
  policy.openFromInfrared(start); CHECK(policy.web(start + 599999));
  policy.noteActivity(start + 599999); CHECK(policy.remaining(start + 600000) == 599999);
  policy.tick(start + 1199999, true); CHECK(!policy.web(start + 1199999));
  policy.tick(start, true); CHECK(!policy.web(start));
}
door::DeviceSettings valid() {
  door::DeviceSettings settings;
  std::strcpy(settings.pairingCode, "59263814");
  std::strcpy(settings.setupPassword, door::kDefaultSetupPassword);
  return settings;
}
void settings_validation() {
  auto settings = valid(); CHECK(door::validSettings(settings));
  CHECK(settings.relayPulseMs == 200);
  for (const auto* pulse : {"1", "375", "800"}) {
    CHECK(!door::updateRelayPulse(settings, pulse)); CHECK(door::validSettings(settings));
  }
  for (const auto* pulse : {"", "0", "801", "-1", "1.5", "2e2", " 200", "200 ", "999999999999", "abc"}) {
    CHECK(door::updateRelayPulse(settings, pulse)); CHECK(settings.relayPulseMs == 800);
  }
  settings.relayPulseMs = 0; CHECK(!door::validSettings(settings));
  settings.relayPulseMs = 801; CHECK(!door::validSettings(settings));
  settings.relayPulseMs = 375;
  CHECK(settings.nfcMaxFailures == 5); CHECK(settings.nfcLockoutSeconds == 60);
  CHECK(!door::updateNfcLimits(settings, "1", "1")); CHECK(door::validSettings(settings));
  CHECK(!door::updateNfcLimits(settings, "20", "3600")); CHECK(door::validSettings(settings));
  for (const auto* value : {"", "0", "21", "-1", "1.5", "1e1", " 5", "4294967297"}) {
    CHECK(door::updateNfcLimits(settings, value, "60")); CHECK(settings.nfcMaxFailures == 20);
  }
  for (const auto* value : {"", "0", "3601", "-1", "1.5", " 60", "4294967297"}) {
    CHECK(door::updateNfcLimits(settings, "5", value)); CHECK(settings.nfcLockoutSeconds == 3600);
    CHECK(settings.nfcMaxFailures == 20);  // Rejected updates are atomic.
  }
  CHECK(!door::updateSettings(settings, "Home", "password1", false, "PrivateSetupPassword"));
  CHECK(std::string(settings.ssid) == "Home"); CHECK(std::string(settings.wifiPassword) == "password1");
  CHECK(!door::updateSettings(settings, "Home", "", false, ""));
  CHECK(std::string(settings.wifiPassword) == "password1");
  auto previous = settings;
  CHECK(door::updateSettings(settings, "Other", "", false, ""));
  CHECK(std::memcmp(&settings, &previous, sizeof(settings)) == 0);
  CHECK(!door::updateSettings(settings, "Open Network", "", true, "")); CHECK(!settings.wifiPassword[0]);
  CHECK(!door::updateSettings(settings, "", "", false, "")); CHECK(!settings.ssid[0]);
  CHECK(!door::updateSettings(settings, "Home", std::string(64, 'a').c_str(), false, ""));
  CHECK(door::validWifiPassword(std::string(64, 'F').c_str()));
  CHECK(!door::validWifiPassword(std::string(64, 'g').c_str()));
  previous = settings;
  for (const auto* password : {"short", "1234567", "password\n", "pass\x01word"})
    CHECK(door::updateSettings(settings, "Home", password, false, ""));
  CHECK(door::updateSettings(settings, std::string(33, 'x').c_str(), "password1", false, ""));
  CHECK(door::updateSettings(settings, "Home\n", "password1", false, ""));
  CHECK(door::updateSettings(settings, "Home", "password1", false, "short"));
  CHECK(door::updateSettings(settings, "Home", "password1", false, std::string(64, 'x').c_str()));
  CHECK(std::memcmp(&settings, &previous, sizeof(settings)) == 0);
  for (const auto* code : {"", "12345678", "87654321", "46637726", "11111111", "abc12345", "123456789"})
    CHECK(!door::validPairingCode(code));
  CHECK(door::validPairingCode("59263814"));
  auto unterminated = valid(); std::memset(unterminated.ssid, 'x', sizeof(unterminated.ssid));
  CHECK(!door::validSettings(unterminated));
}
void resetNvs() {
  nvsRecord.clear(); nvsPending.clear(); nvsWrites = 0;
  nvsOpenFails = nvsReadFails = nvsWriteFails = nvsCommitFails = nvsCorruptReadback = false;
}
void settings_storage() {
  resetNvs(); SettingsStore store; auto settings = valid();
  CHECK(store.load(settings) == SettingsStore::Load::Missing);
  CHECK(store.save(settings)); CHECK(nvsWrites == 1);
  settings.relayPulseMs = 537;
  settings.nfcMaxFailures = 3; settings.nfcLockoutSeconds = 120;
  CHECK(!door::updateSettings(settings, "Home", "password1", false, "")); CHECK(store.save(settings));
  SettingsStore reboot; auto restored = valid();
  CHECK(reboot.load(restored) == SettingsStore::Load::Loaded);
  CHECK(restored.relayPulseMs == 537);
  CHECK(std::string(settings.ssid) == restored.ssid);
  CHECK(std::string(settings.wifiPassword) == restored.wifiPassword);
  CHECK(std::string(settings.pairingCode) == restored.pairingCode);
  CHECK(std::string(settings.setupPassword) == restored.setupPassword);
  CHECK(settings.relayPulseMs == restored.relayPulseMs);
  CHECK(restored.nfcMaxFailures == 3); CHECK(restored.nfcLockoutSeconds == 120);
  const auto goodRecord = nvsRecord;
  for (size_t byte = 0; byte < goodRecord.size(); ++byte) {
    nvsRecord = goodRecord; nvsRecord[byte] ^= 1;
    SettingsStore corrupt; auto output = valid();
    CHECK(corrupt.load(output) == SettingsStore::Load::Error);
    CHECK(corrupt.save(settings));
    CHECK(corrupt.load(output) == SettingsStore::Load::Loaded);
  }
  for (int mode = 0; mode < 3; ++mode) {
    nvsRecord = goodRecord;
    if (mode == 0) nvsRecord.pop_back();
    if (mode == 1) nvsRecord.push_back(0);
    if (mode == 2) nvsReadFails = true;
    SettingsStore unreadable; CHECK(unreadable.load(restored) == SettingsStore::Load::Error);
    nvsReadFails = false;
  }
  nvsRecord = goodRecord;
  for (int mode = 0; mode < 3; ++mode) {
    nvsWriteFails = mode == 0; nvsCommitFails = mode == 1; nvsCorruptReadback = mode == 2;
    CHECK(!store.save(settings));
    nvsWriteFails = nvsCommitFails = nvsCorruptReadback = false;
  }
  auto invalid = settings; invalid.setupPassword[0] = 0; CHECK(!store.save(invalid));
  nvsOpenFails = true; SettingsStore unavailable;
  CHECK(unavailable.load(restored) == SettingsStore::Load::Error); CHECK(!unavailable.save(settings));
}
void legacy_settings() {
  resetNvs(); SettingsStore store; auto settings = valid();
  CHECK(!door::updateSettings(settings, "Existing Home", "keep-password", false, "PrivateSetupPassword"));
  CHECK(store.load(settings) == SettingsStore::Load::Missing); CHECK(store.save(settings));
  // Version 1 has the same 171 credential bytes, followed immediately by CRC.
  nvsRecord.resize(179); nvsRecord[2] = 1;
  uint32_t crc = 0xffffffffu;
  for (size_t i = 0; i < 175; ++i) {
    crc ^= nvsRecord[i];
    for (int bit = 0; bit < 8; ++bit) crc = (crc >> 1) ^ ((crc & 1) ? 0xedb88320u : 0u);
  }
  crc = ~crc;
  for (int i = 0; i < 4; ++i) nvsRecord[175 + i] = uint8_t(crc >> (8 * i));
  const auto legacy = nvsRecord;
  SettingsStore upgrade; auto restored = valid(); restored.relayPulseMs = 800;
  CHECK(upgrade.load(restored) == SettingsStore::Load::Loaded);
  CHECK(restored.relayPulseMs == 200);
  CHECK(std::string(restored.ssid) == "Existing Home");
  CHECK(std::string(restored.wifiPassword) == "keep-password");
  CHECK(std::string(restored.setupPassword) == "PrivateSetupPassword");
  CHECK(std::string(restored.pairingCode) == "59263814");
  CHECK(nvsRecord == legacy);  // Loading does not write or risk credentials.
  restored.relayPulseMs = 650; CHECK(upgrade.save(restored));
  SettingsStore reboot; CHECK(reboot.load(restored) == SettingsStore::Load::Loaded);
  CHECK(restored.relayPulseMs == 650); CHECK(nvsRecord.size() == 191); CHECK(nvsRecord[2] == 3);
  for (size_t i = 0; i < legacy.size(); ++i) {
    nvsRecord = legacy; nvsRecord[i] ^= 1;
    SettingsStore bad; CHECK(bad.load(restored) == SettingsStore::Load::Error);
  }
}
void version2_settings() {
  resetNvs(); SettingsStore store; auto settings = valid(); settings.relayPulseMs = 650;
  CHECK(store.load(settings) == SettingsStore::Load::Missing); CHECK(store.save(settings));
  nvsRecord.resize(183); nvsRecord[2] = 2;
  uint32_t crc = 0xffffffffu;
  for (size_t i = 0; i < 179; ++i) {
    crc ^= nvsRecord[i];
    for (int bit = 0; bit < 8; ++bit) crc = (crc >> 1) ^ ((crc & 1) ? 0xedb88320u : 0u);
  }
  crc = ~crc; for (int i = 0; i < 4; ++i) nvsRecord[179 + i] = uint8_t(crc >> (8 * i));
  const auto before = nvsRecord;
  SettingsStore upgrade; settings.nfcMaxFailures = 20; settings.nfcLockoutSeconds = 3600;
  CHECK(upgrade.load(settings) == SettingsStore::Load::Loaded);
  CHECK(settings.relayPulseMs == 650); CHECK(settings.nfcMaxFailures == 5); CHECK(settings.nfcLockoutSeconds == 60);
  CHECK(nvsRecord == before);
}
int main() {
  setup_window_and_recovery(); first_boot_and_rollover(); infrared_and_user_activity_windows();
  settings_validation(); settings_storage(); legacy_settings(); version2_settings();
  std::cout << "PASS: setup window, persistent offline AP recovery, Wi-Fi validation and actual settings NVS adapter.\n";
}
