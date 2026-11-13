#include <Arduino.h>
#include <WiFi.h>
#include <HomeSpan.h>
#define NO_LED_FEEDBACK_CODE
#include <IRremote.hpp>
#include <atomic>
#include "Controller.h"
#include "TimedRelay.h"
#if __has_include("secrets.h")
#include "secrets.h"
#else
#include "secrets.example.h"
#endif

TimedRelay relay;
door::Controller controller(relay);
std::atomic<bool> networkLost{false};
bool configured = false;
bool verifiedSession = false;  // Only changed on HomeSpan's polling task.

bool validCredentials() {
  const char* code = DOOR_PAIRING_CODE;
  if (!strlen(DOOR_WIFI_SSID) || strlen(DOOR_WIFI_PASSWORD) < 8 || strlen(code) != 8)
    return false;
  bool same = true;
  for (int i = 0; i < 8; ++i) {
    if (code[i] < '0' || code[i] > '9') return false;
    if (code[i] != code[0]) same = false;
  }
  return !same && strcmp(code, "46637726") && strcmp(code, "12345678") &&
         strcmp(code, "87654321");
}

void syncConnectivity() {
  // Consume the loss latch even if a fast reconnect already restored Wi-Fi.
  if (networkLost.exchange(false)) {
    relay.permit(false);
    controller.connectivity(millis(), false, false);
  }
  bool online = WiFi.status() == WL_CONNECTED;
  relay.permit(online && verifiedSession);
  controller.connectivity(millis(), online, verifiedSession);
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
    if (on->getVal<bool>() && !controller.actionOn()) on->setVal(false);
  }
};

void setup() {
  // First hardware action; no USB/serial wait or network initialization first.
  bool timerReady = relay.begin();
  controller.boot(millis());
  Serial.begin(115200);
  if (!timerReady || !validCredentials()) {
    controller.trip(millis());
    Serial.println("Relay OFF. Configure private secrets.h and verify timer before use.");
    return;
  }
  WiFi.onEvent([](WiFiEvent_t event) {
    if (event == ARDUINO_EVENT_WIFI_STA_DISCONNECTED || event == ARDUINO_EVENT_WIFI_STA_LOST_IP) {
      relay.permit(false);
      networkLost.store(true);
    }
  });
  // No serial command interface, AP provisioning, OTA or public unlock API.
  homeSpan.setLogLevel(-1);
  homeSpan.setSerialInputDisable(true);
  homeSpan.setApFunction([]() { relay.permit(false); });
  homeSpan.setWifiCredentials(DOOR_WIFI_SSID, DOOR_WIFI_PASSWORD);
  homeSpan.setPairingCode(DOOR_PAIRING_CODE);
  homeSpan.setStatusCallback([](HS_STATUS status) {
    verifiedSession = status == HS_CONNECTED;
    syncConnectivity();
  });
  homeSpan.begin(Category::Switches, "Front Door Release", "door-release", "C3-Release");
  new SpanAccessory();
  new Service::AccessoryInformation();
  new Characteristic::Identify();
  new Characteristic::Name("Front Door Release");
  new Characteristic::Manufacturer("DIY");
  new Characteristic::Model("ESP32-C3 SuperMini");
  new Characteristic::SerialNumber("door-release-001");
  new Characteristic::FirmwareRevision("1.0.0");
  new ReleaseSwitch();
  IrReceiver.begin(door::kIrPin, DISABLE_LED_FEEDBACK);
  configured = true;
}

void loop() {
  if (!configured) { relay.stop(); delay(10); return; }
  syncConnectivity();
  controller.tick(millis());
  homeSpan.poll();
  syncConnectivity();
  controller.tick(millis());
  if (IrReceiver.decode()) {
    const auto data = IrReceiver.decodedIRData;
    IrReceiver.resume();
    // Ignore unknown/repeat/overflow frames and limit diagnostic log rate.
    static uint32_t lastLog = 0;
    if (data.protocol != UNKNOWN &&
        !(data.flags & (IRDATA_FLAGS_IS_REPEAT | IRDATA_FLAGS_WAS_OVERFLOW)) &&
        uint32_t(millis() - lastLog) >= 250) {
      lastLog = millis();
      Serial.printf("ir_diagnostic: protocol=%d address=0x%X command=0x%X result=%s\n",
                    int(data.protocol), data.address, data.command,
                    door::name(controller.infrared(millis())));
    }
  }
  delay(1);
}
