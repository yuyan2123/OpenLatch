#include <Arduino.h>
#include <WiFi.h>
#include <HomeSpan.h>
#include <HK_HomeKit.h>
#define NO_LED_FEEDBACK_CODE
#include <IRremote.hpp>
#include <atomic>
#include "Controller.h"
#include "TimedRelay.h"
#include <TapLed.h>
#include "IrInput.h"
#include "IrStore.h"
#include "SerialLine.h"
#include "SetupPortal.h"
#include "LanSubnetApi.h"
#include "HomeKeyManager.h"
#include "HomeKeyServices.h"
#include <HAP.h>
#include "FirmwareBoot.h"
#if __has_include("secrets.h")
#include "secrets.h"
#else
#include "secrets.example.h"
#endif
#ifndef DOOR_SETUP_PASSWORD
#define DOOR_SETUP_PASSWORD "DoorSetup-2026"
#endif

TimedRelay relay;
constexpr gpio_num_t kNfcLedPin = GPIO_NUM_8;
TapLed nfcLed(kNfcLedPin, false);
door::Controller controller(relay);
IrStore irStore;
door::Infrared infrared(irStore);
IrStore setupIrStore("door_setup_ir");
door::Infrared setupInfrared(setupIrStore);
SerialLine serialLine;
door::DeviceSettings settings;
SettingsStore settingsStore;
door::NfcAttemptLimiter nfcAttempts;
door::RfidFobs fobs;
HomeKeyStore homeKeyStore;
HomeKeyManager homeKey(homeKeyStore, nfcAttempts);
SetupPortal setupPortal(settings, settingsStore, infrared, setupInfrared, fobs, controller, relay, nfcAttempts);
LanSubnetApi lanSubnetApi;
std::atomic<bool> networkLost{false};
bool hardwareReady = false;
bool homekitStarted = false;
bool settingsReady = false;
bool verifiedSession = false;  // Only changed on HomeSpan's polling task.
bool firmwarePaused = false;
FirmwareBoot firmwareBoot;
bool firmwareWatchdog = false;
// Override Arduino's weak hook; confirmation belongs to our application checks.
extern "C" bool verifyRollbackLater() { return true; }

bool initializeSettings() {
  const auto loaded = settingsStore.load(settings);
  if (loaded == SettingsStore::Load::Loaded) return true;
  settings = {};
  snprintf(settings.setupPassword, sizeof(settings.setupPassword), "%s",
           door::printable(DOOR_SETUP_PASSWORD, 12, 63) ? DOOR_SETUP_PASSWORD : door::kDefaultSetupPassword);
  if (door::validPairingCode(DOOR_PAIRING_CODE))
    snprintf(settings.pairingCode, sizeof(settings.pairingCode), "%s", DOOR_PAIRING_CODE);
  else {
    do {
      snprintf(settings.pairingCode, sizeof(settings.pairingCode), "%08lu",
               static_cast<unsigned long>(esp_random() % 100000000));
    } while (!door::validPairingCode(settings.pairingCode));
  }
  if (loaded == SettingsStore::Load::Error) {
    Serial.println("setup_storage: invalid; use the setup AP and save settings to repair");
    return false;
  }
  if (strlen(DOOR_WIFI_SSID) <= 32 && strlen(DOOR_WIFI_PASSWORD) <= 64 &&
      door::validWifiPassword(DOOR_WIFI_PASSWORD)) {
    snprintf(settings.ssid, sizeof(settings.ssid), "%s", DOOR_WIFI_SSID);
    snprintf(settings.wifiPassword, sizeof(settings.wifiPassword), "%s", DOOR_WIFI_PASSWORD);
  }
  if (!door::validSettings(settings)) { settings.ssid[0] = 0; settings.wifiPassword[0] = 0; }
  return settingsStore.save(settings);
}

void syncConnectivity() {
  if (!homekitStarted) return;
  // Consume the loss latch even if a fast reconnect already restored Wi-Fi.
  if (networkLost.exchange(false)) {
    relay.permit(false);
    controller.connectivity(millis(), false, false);
  }
  bool online = WiFi.status() == WL_CONNECTED;
  relay.permit(online && verifiedSession && !setupPortal.restarting() && !setupPortal.updating());
  controller.connectivity(millis(), online, verifiedSession);
}

void syncInfrared() {
  controller.inhibit(millis(), setupPortal.learning() || setupPortal.restarting() || setupPortal.updating());
  relay.permitInfrared(hardwareReady && infrared.enabled() && !setupPortal.learning() &&
                       !controller.faulted() && !setupPortal.restarting() && !setupPortal.updating());
}

bool syncNfc() {
  const bool available = hardwareReady && !controller.faulted() && !setupPortal.restarting() && !setupPortal.updating();
  const bool unlockAllowed = available && !setupPortal.learning();
  homeKey.enabled(unlockAllowed);
  relay.permitNfc(unlockAllowed);
  homeKey.fobAdmission(fobs.revision(), available && fobs.ready() && !setupPortal.irLearning() &&
                       (fobs.learning() || fobs.count() > 0));
  return unlockAllowed;
}

void releaseFromNfc(const char* credential) {
  const auto result = controller.nfc(millis(), true);
  if (result == door::Result::Accepted) nfcLed.flash();
  Serial.printf("command_received: %s; result=%s\n", credential, door::name(result));
}

bool initializeNfcLed() {
  nvs_handle_t handle;
  if (nvs_open("door_led", NVS_READONLY, &handle) == ESP_OK) {
    uint8_t activeHigh = 0;
    if (nvs_get_u8(handle, "active_high", &activeHigh) == ESP_OK) nfcLed.setActiveHigh(activeHigh == 1);
    nvs_close(handle);
  }
  return nfcLed.begin() && gpio_input_enable(kNfcLedPin) == ESP_OK;
}

void ledStatus() {
  gpio_io_config_t config{};
  if (gpio_get_io_config(kNfcLedPin, &config) == ESP_OK)
    Serial.printf("nfc_led: gpio=8 active=%s level=%d output=%d\n",
                  nfcLed.activeHigh() ? "high" : "low", gpio_get_level(kNfcLedPin), config.oe);
}

void setNfcLedPolarity(bool activeHigh) {
  const bool previous = nfcLed.activeHigh();
  if (!nfcLed.setActiveHigh(activeHigh)) { Serial.println("nfc_led: busy"); return; }
  nvs_handle_t handle;
  bool saved = false;
  if (nvs_open("door_led", NVS_READWRITE, &handle) == ESP_OK) {
    saved = nvs_set_u8(handle, "active_high", uint8_t(activeHigh)) == ESP_OK && nvs_commit(handle) == ESP_OK;
    nvs_close(handle);
  }
  if (!saved) {
    nfcLed.setActiveHigh(previous);
    Serial.println("nfc_led: polarity could not be saved");
  }
  ledStatus();
}

void irStatus() {
  gpio_io_config_t pinConfig{};
  if (gpio_get_io_config(static_cast<gpio_num_t>(door::kRelayPin), &pinConfig) == ESP_OK)
    Serial.printf("relay_pin: pin=4 function=%lu output=%d drive=%lu\n",
                  static_cast<unsigned long>(pinConfig.fun_sel), pinConfig.oe,
                  static_cast<unsigned long>(pinConfig.drv));
  const auto& key = infrared.key();
  Serial.printf("ir_status: hardware=%s storage=%s learned=%s learning=%s enabled=%s "
                "protocol=%s address=0x%X command=0x%X bits=%u\n",
                hardwareReady ? "ready" : "fault", infrared.ready() ? "ready" : "error",
                key.valid() ? "yes" : "no", infrared.learning() ? "yes" : "no",
                hardwareReady && infrared.enabled() && !controller.faulted() ? "yes" : "no",
                door::name(key.protocol), key.address, key.command, key.bits);
  Serial.printf("ir_buttons: door=%u/%u setup=%u/%u\n", infrared.count(), door::kIrMaxKeys,
                setupInfrared.count(), door::kIrMaxKeys);
}

void serialCommand(const char* command) {
  if (!strcmp(command, "led status")) ledStatus();
  else if (!strcmp(command, "led test")) nfcLed.flash();
  else if (!strcmp(command, "led active-high")) setNfcLedPolarity(true);
  else if (!strcmp(command, "led active-low")) setNfcLedPolarity(false);
  else if (!strcmp(command, "ir status")) irStatus();
  else if (!strcmp(command, "ir help"))
    Serial.println("Commands: ir status | ir learn | ir cancel | ir forget | ir help");
  else if (!strcmp(command, "ir learn")) {
    if (setupInfrared.learning() || fobs.learning() || !hardwareReady || controller.faulted() || !infrared.learn(millis())) {
      Serial.println("ir_learning: unavailable; check storage/timer and reboot");
      return;
    }
    syncInfrared();
    Serial.println("ir_learning: started; release all buttons for 500 ms, then press the same "
                   "button twice, releasing for at least 500 ms between presses; timeout=30s; relay inhibited");
  } else if (!strcmp(command, "ir cancel")) {
    infrared.cancel(millis());
    syncInfrared();
    Serial.println("ir_learning: cancelled; previous key retained");
  } else if (!strcmp(command, "ir forget")) {
    // Stop local access before committing the disabled record.
    relay.permitInfrared(false);
    const bool saved = infrared.forget(millis());
    syncInfrared();
    Serial.println(saved ? "ir_key: erased; infrared unlock disabled" :
                          "ir_key: erase failed; infrared disabled; stored key may return after reboot; retry ir forget");
  } else Serial.println("Unknown command. Use: ir help");
}

void pollSerial() {
  // Do not block HomeSpan or the relay timer on an incomplete command.
  for (int budget = 32; budget > 0 && Serial.available(); --budget) {
    const auto result = serialLine.feed(char(Serial.read()));
    if (result == SerialLine::Result::Complete) serialCommand(serialLine.value());
    else if (result == SerialLine::Result::Rejected)
      Serial.println("Command rejected: invalid or oversized line");
  }
}

struct ReleaseSwitch : Service::Switch {
  SpanCharacteristic* on;
  ReleaseSwitch() : Service::Switch() {
    new Characteristic::Name("OpenLatch");
    on = new Characteristic::On(false);  // Never persist a release request.
  }
  boolean update() override {
    syncConnectivity();
    // HomeSpan invokes update only after HAP verifies the encrypted session.
    auto result = controller.homekit(millis(), verifiedSession, on->getNewVal());
    Serial.printf("command_received: homekit; result=%s; relay_commanded=%s\n",
                  door::name(result), controller.actionOn() ? "ON" : "OFF");
    return result == door::Result::Accepted || result == door::Result::Off;
  }
  void loop() override {
    controller.tick(millis());
    const bool active = controller.actionOn();
    if (on->getVal<bool>() != active) on->setVal(active);
  }
};

void setup() {
  // First hardware action; no USB/serial wait or network initialization first.
  bool timerReady = relay.begin();
  const uint32_t bootAt = millis();
  firmwareBoot.begin(bootAt);
  if (firmwareBoot.pending()) { enableLoopWDT(); firmwareWatchdog = true; }
  setupPortal.firmwareHealth(firmwareBoot);
  controller.boot(bootAt);
  Serial.begin(115200);
  if (!initializeNfcLed()) Serial.println("nfc_led: unavailable");
  if (!timerReady) {
    controller.trip(millis());
    Serial.println("Relay OFF. Timer initialization failed; reboot after repair.");
  }
  hardwareReady = timerReady;
  if (!infrared.begin(millis()))
    Serial.println("ir_storage: error; infrared disabled until storage is repaired and device is rebooted");
  if (!setupInfrared.begin(millis()))
    Serial.println("setup_ir_storage: error; repair the setup button from the setup page");
  if (!fobs.begin())
    Serial.println("rfid_storage: error; clear all fobs from the setup page to repair");
  if (hardwareReady) IrReceiver.begin(door::kIrPin, DISABLE_LED_FEEDBACK);
  syncInfrared();
  Serial.println("USB infrared setup ready. Use: ir help");
  irStatus();
  settingsReady = initializeSettings();
  nfcAttempts.begin(settings.nfcMaxFailures, settings.nfcLockoutSeconds);
  controller.boot(bootAt, settings.relayPulseMs);
  if (!hardwareReady) controller.trip(millis());
  WiFi.onEvent([](WiFiEvent_t event) {
    if (event == ARDUINO_EVENT_WIFI_STA_DISCONNECTED || event == ARDUINO_EVENT_WIFI_STA_LOST_IP) {
      relay.permit(false);
      networkLost.store(true);
    }
  });
  setupPortal.beforeFirmwareUpdate([]() {
    homeKey.enabled(false);
    homeKey.fobAdmission(fobs.revision(), false);
    if (homekitStarted) HAPClient::tearDown(nullptr);
    verifiedSession = false;
    if (hardwareReady) IrReceiver.stop();
    firmwarePaused = true;
  });
  setupPortal.begin(bootAt, settingsReady, hardwareReady);
  lanSubnetApi.begin();
  uint8_t stationMac[6]{};
  if (esp_read_mac(stationMac, ESP_MAC_WIFI_STA) == ESP_OK)
    Serial.printf("nfc_station_mac: %02X:%02X:%02X:%02X:%02X:%02X\n", stationMac[0], stationMac[1], stationMac[2], stationMac[3], stationMac[4], stationMac[5]);
  homeKey.begin();
  if (!hardwareReady || !settingsReady || !settings.ssid[0]) {
    Serial.println("HomeKit not started. Configure Wi-Fi using the setup page and restart.");
    return;
  }
  // The setup portal owns Wi-Fi attempts; HomeSpan must not start competing ones.
  homeSpan.setLogLevel(-1);
  homeSpan.setSerialInputDisable(true);
  homeSpan.setApFunction([]() { relay.permit(false); });
  homeSpan.setWifiBegin([](const char*, const char*) {});
  homeSpan.setWifiCredentials(settings.ssid, settings.wifiPassword);
  homeSpan.setPairingCode(settings.pairingCode);
  // HomeSpan defaults to port 80; reserve that port for the setup website.
  homeSpan.setPortNum(1201);
  homeSpan.setHostNameSuffix("");
  homeSpan.setStatusCallback([](HS_STATUS status) {
    verifiedSession = status == HS_CONNECTED;
    syncConnectivity();
  });
  homekitStarted = true;
  homeSpan.setControllerCallback(homeKeyControllersChanged);
  homeSpan.begin(Category::Locks, "OpenLatch", setupPortal.hostname(), "OpenLatch");
  new SpanAccessory();
  new HomeKeyAccessoryInfo();
  new Characteristic::Identify();
  new Characteristic::Name("OpenLatch");
  new Characteristic::Manufacturer("DIY");
  new Characteristic::Model("ESP32-C3 SuperMini");
  new Characteristic::SerialNumber("door-release-001");
  auto* revision = new Characteristic::FirmwareRevision(door::kFirmwareVersion);
  // Add Home Key finish without changing the existing switch's IIDs.
  const uint32_t switchIid = revision->getIID() + 1;
  homeSpan.resetIID(1000);
  uint8_t finishBytes[]{1, 4, 0xe3, 0xe3, 0xe3, 0};
  TLV8 finish(nullptr, 0); finish.unpack(finishBytes, sizeof(finishBytes));
  new Characteristic::HardwareFinish(finish);
  homeSpan.resetIID(switchIid);
  new ReleaseSwitch();
  new HomeKeyLockManagement();
  new HomeKeyLockService(controller);
  new HomeKeyAccessService(homeKeyStore);
}

void loop() {
  syncNfc();
  setupPortal.nfcStatus(homeKey.configured(), homeKey.provisioned(), homeKey.connected(), homeKey.transport());
  if (!setupPortal.restarting() && !setupPortal.updating()) pollSerial();
  setupPortal.poll();
  lanSubnetApi.poll();
  if (firmwarePaused && !setupPortal.updating() && !setupPortal.restarting()) {
    // Drop buffered IR and require a new quiet interval after a failed upload.
    infrared.cancel(millis()); setupInfrared.cancel(millis());
    if (hardwareReady) IrReceiver.start();
    firmwarePaused = false;
  }
  firmwareBoot.poll(millis(), hardwareReady && settingsReady && !controller.faulted() &&
                    infrared.ready() && setupInfrared.ready() && homeKey.healthy());
  if (firmwareWatchdog && firmwareBoot.healthy()) { disableLoopWDT(); firmwareWatchdog = false; }
  if (setupPortal.restartDue(millis())) { relay.stop(); ESP.restart(); return; }
  if (!hardwareReady) { relay.stop(); delay(10); return; }
  syncInfrared();
  syncConnectivity();
  controller.tick(millis());
  if (homekitStarted) homeSpan.poll();
  if (homekitStarted) syncHomeKeyControllers(homeKeyStore);
  // A setting/learning action in this iteration revokes pending NFC results.
  const bool nfcAllowed = syncNfc();
  if (!homeKeyControllersPending && homeKey.takeVerified(millis()) && nfcAllowed) releaseFromNfc("homekey");
  door::RfidUid uid;
  if (homeKey.takeFob(millis(), uid)) {
    if (setupPortal.receiveFob(millis(), uid) && nfcAllowed) releaseFromNfc("rfid");
    syncInfrared();
  }
  syncConnectivity();
  controller.tick(millis());
  if (!setupPortal.restarting() && !setupPortal.updating() && IrReceiver.decode()) {
    const auto data = IrReceiver.decodedIRData;
    IrReceiver.resume();
    const auto event = setupPortal.receiveIr(millis(), irKey(data), irRepeat(data), irInvalid(data));
    syncInfrared();
    if (event == door::IrEvent::Matched) {
      const auto result = controller.infrared(millis(), true);
      Serial.printf("command_received: infrared; result=%s; relay_commanded=%s\n",
                    door::name(result), controller.actionOn() ? "ON" : "OFF");
    } else if (event != door::IrEvent::Ignored) {
      Serial.printf("ir_learning: %s\n", door::name(event));
      if (event == door::IrEvent::Captured)
        Serial.println("Release for at least 500 ms, then press the same button again.");
      if (event == door::IrEvent::Learned) irStatus();
    }
    // Rate limiting applies only to diagnostics, never to authorization.
    static uint32_t lastLog = 0;
    if (!irRepeat(data) && uint32_t(millis() - lastLog) >= 250) {
      lastLog = millis();
      Serial.printf("ir_diagnostic: protocol=%d address=0x%X command=0x%X bits=%u flags=0x%X supported=%s\n",
                    int(data.protocol), data.address, data.command, data.numberOfBits,
                    data.flags, irKey(data).valid() && !irInvalid(data) ? "yes" : "no");
    }
  }
  delay(1);
}
