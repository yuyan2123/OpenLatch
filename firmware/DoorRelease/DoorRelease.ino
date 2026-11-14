#include <Arduino.h>
#include <WiFi.h>
#include <HomeSpan.h>
#define NO_LED_FEEDBACK_CODE
#include <IRremote.hpp>
#include <atomic>
#include "Controller.h"
#include "TimedRelay.h"
#include "IrInput.h"
#include "IrStore.h"
#include "SerialLine.h"
#include "SetupPortal.h"
#if __has_include("secrets.h")
#include "secrets.h"
#else
#include "secrets.example.h"
#endif
#ifndef DOOR_SETUP_PASSWORD
#define DOOR_SETUP_PASSWORD "DoorSetup-2026"
#endif

TimedRelay relay;
door::Controller controller(relay);
IrStore irStore;
door::Infrared infrared(irStore);
IrStore setupIrStore("door_setup_ir");
door::Infrared setupInfrared(setupIrStore);
SerialLine serialLine;
door::DeviceSettings settings;
SettingsStore settingsStore;
SetupPortal setupPortal(settings, settingsStore, infrared, setupInfrared, controller, relay);
std::atomic<bool> networkLost{false};
bool hardwareReady = false;
bool homekitStarted = false;
bool settingsReady = false;
bool verifiedSession = false;  // Only changed on HomeSpan's polling task.

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
  relay.permit(online && verifiedSession && !setupPortal.restarting());
  controller.connectivity(millis(), online, verifiedSession);
}

void syncInfrared() {
  controller.inhibit(millis(), setupPortal.irLearning() || setupPortal.restarting());
  relay.permitInfrared(hardwareReady && infrared.enabled() && !setupPortal.irLearning() &&
                       !controller.faulted() && !setupPortal.restarting());
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
  if (!strcmp(command, "ir status")) irStatus();
  else if (!strcmp(command, "ir help"))
    Serial.println("Commands: ir status | ir learn | ir cancel | ir forget | ir help");
  else if (!strcmp(command, "ir learn")) {
    if (setupInfrared.learning() || !hardwareReady || controller.faulted() || !infrared.learn(millis())) {
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
    new Characteristic::Name("Front Door Release");
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
  controller.boot(bootAt);
  Serial.begin(115200);
  if (!timerReady) {
    controller.trip(millis());
    Serial.println("Relay OFF. Timer initialization failed; reboot after repair.");
  }
  hardwareReady = timerReady;
  if (!infrared.begin(millis()))
    Serial.println("ir_storage: error; infrared disabled until storage is repaired and device is rebooted");
  if (!setupInfrared.begin(millis()))
    Serial.println("setup_ir_storage: error; repair the setup button from the setup page");
  if (hardwareReady) IrReceiver.begin(door::kIrPin, DISABLE_LED_FEEDBACK);
  syncInfrared();
  Serial.println("USB infrared setup ready. Use: ir help");
  irStatus();
  settingsReady = initializeSettings();
  WiFi.onEvent([](WiFiEvent_t event) {
    if (event == ARDUINO_EVENT_WIFI_STA_DISCONNECTED || event == ARDUINO_EVENT_WIFI_STA_LOST_IP) {
      relay.permit(false);
      networkLost.store(true);
    }
  });
  setupPortal.begin(bootAt, settingsReady, hardwareReady);
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
  homeSpan.begin(Category::Switches, "Front Door Release", setupPortal.hostname(), "C3-Release");
  new SpanAccessory();
  new Service::AccessoryInformation();
  new Characteristic::Identify();
  new Characteristic::Name("Front Door Release");
  new Characteristic::Manufacturer("DIY");
  new Characteristic::Model("ESP32-C3 SuperMini");
  new Characteristic::SerialNumber("door-release-001");
  new Characteristic::FirmwareRevision("1.2.2-multi-ir");
  new ReleaseSwitch();
}

void loop() {
  if (!setupPortal.restarting()) pollSerial();
  setupPortal.poll();
  if (setupPortal.restartDue(millis())) { relay.stop(); ESP.restart(); return; }
  if (!hardwareReady) { relay.stop(); delay(10); return; }
  syncInfrared();
  syncConnectivity();
  controller.tick(millis());
  if (homekitStarted) homeSpan.poll();
  syncConnectivity();
  controller.tick(millis());
  if (!setupPortal.restarting() && IrReceiver.decode()) {
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
