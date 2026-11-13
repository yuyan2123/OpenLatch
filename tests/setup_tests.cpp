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
door::DeviceSettings valid() {
  door::DeviceSettings settings;
  std::strcpy(settings.pairingCode, "59263814");
  std::strcpy(settings.setupPassword, door::kDefaultSetupPassword);
  return settings;
}
void settings_validation() {
  auto settings = valid(); CHECK(door::validSettings(settings));
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
  CHECK(!door::updateSettings(settings, "Home", "password1", false, "")); CHECK(store.save(settings));
  SettingsStore reboot; auto restored = valid();
  CHECK(reboot.load(restored) == SettingsStore::Load::Loaded);
  CHECK(std::memcmp(&settings, &restored, sizeof(settings)) == 0);
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
int main() {
  setup_window_and_recovery(); first_boot_and_rollover(); settings_validation(); settings_storage();
  std::cout << "PASS: setup window, persistent offline AP recovery, Wi-Fi validation and actual settings NVS adapter.\n";
}
