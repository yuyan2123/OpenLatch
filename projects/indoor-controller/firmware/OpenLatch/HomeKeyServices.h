#pragma once
#include <HomeSpan.h>
#include <HK_HomeKit.h>
#include <mbedtls/sha256.h>
#include "HomeKeyStore.h"
#include "HomeKeyCrypto.h"
#include "Controller.h"

CUSTOM_CHAR(ConfigurationState,263,PR+EV,UINT16,0,0,1,true)
CUSTOM_CHAR(HardwareFinish,26C,PR,TLV_ENC,NULL_TLV,NULL_TLV,NULL_TLV,true)
CUSTOM_CHAR(NFCAccessControlPoint,264,PR+PW+WR,TLV_ENC,NULL_TLV,NULL_TLV,NULL_TLV,true)
CUSTOM_CHAR(NFCAccessSupportedConfiguration,265,PR,TLV_ENC,NULL_TLV,NULL_TLV,NULL_TLV,true)
CUSTOM_CHAR(LockControlPoint,19,PW,TLV_ENC,NULL_TLV,NULL_TLV,NULL_TLV,true)
CUSTOM_SERV(NFCAccess,266)
CUSTOM_SERV(LockManagement,44)

inline bool homeKeyControllersPending = true;
inline void homeKeyControllersChanged() { homeKeyControllersPending = true; }
inline void syncHomeKeyControllers(HomeKeyStore& store) {
  if (!homeKeyControllersPending) return;
  std::unique_lock<std::mutex> lock(store.mutex, std::try_to_lock);
  if (!lock) return;
  homeKeyControllersPending = false;
  if (homeSpan.controllerListBegin() == homeSpan.controllerListEnd()) { store.clearAll(); return; }
  std::vector<ddk::Issuer> current;
  bool changed = false;
  for (auto controller = homeSpan.controllerListBegin(); controller != homeSpan.controllerListEnd(); ++controller) {
    std::vector<uint8_t> input{'k','e','y','-','i','d','e','n','t','i','f','i','e','r'};
    input.insert(input.end(), controller->getLTPK(), controller->getLTPK() + 32);
    uint8_t hash[32]{}; if (mbedtls_sha256(input.data(), input.size(), hash, 0)) { store.clearAll(); return; }
    const std::vector<uint8_t> id(hash, hash + 8);
    const auto found = std::find_if(store.issuerList().begin(), store.issuerList().end(), [&](const auto& issuer) { return issuer.id == id; });
    if (found != store.issuerList().end()) current.push_back(*found);
    else { ddk::Issuer issuer; issuer.id = id; issuer.public_key.assign(controller->getLTPK(), controller->getLTPK() + 32); current.push_back(std::move(issuer)); changed = true; }
  }
  if (changed || current.size() != store.issuerList().size()) { store.issuerList() = std::move(current); store.save(); }
}

struct HomeKeyAccessoryInfo final : Service::AccessoryInformation {
  HomeKeyAccessoryInfo() { opt.push_back(&_CUSTOM_HardwareFinish); }
};

struct HomeKeyAccessService final : Service::NFCAccess {
  HomeKeyStore& store;
  SpanCharacteristic* point;
  explicit HomeKeyAccessService(HomeKeyStore& input) : store(input) {
    req.push_back(&_CUSTOM_ConfigurationState); req.push_back(&_CUSTOM_NFCAccessControlPoint);
    req.push_back(&_CUSTOM_NFCAccessSupportedConfiguration);
    new Characteristic::ConfigurationState();
    TLV8 supported(nullptr, 0); supported.add(1, 0x10); supported.add(2, 0x10);
    new Characteristic::NFCAccessSupportedConfiguration(supported);
    point = new Characteristic::NFCAccessControlPoint();
  }
  boolean update() override {
    if (!point->updated()) return true;
    if (!homeKeyCryptoReady()) return false;
    std::unique_lock<std::mutex> lock(store.mutex, std::try_to_lock);
    if (!lock) return false;
    TLV8 input(nullptr, 0); point->getNewTLV(input);
    if (!input.pack_size() || input.pack_size() > 2048) return false;
    std::vector<uint8_t> bytes(input.pack_size()); input.pack(bytes.data());
    HK_HomeKit provisioning(store, [this]() { store.clearIdentity(); }, bytes);
    auto output = provisioning.processResult();
    if (!store.healthy() || output.empty()) return false;
    TLV8 response(nullptr, 0); response.unpack(output.data(), output.size()); point->setTLV(response, false);
    return true;
  }
};
struct HomeKeyLockManagement final : Service::LockManagement {
  HomeKeyLockManagement() { req.push_back(&_CUSTOM_LockControlPoint); req.push_back(&hapChars.Version); new Characteristic::LockControlPoint(); new Characteristic::Version(); }
};
struct HomeKeyLockService final : Service::LockMechanism {
  door::Controller& controller;
  SpanCharacteristic* target;
  explicit HomeKeyLockService(door::Controller& input) : controller(input) {
    setPrimary();
    // No door/latch sensor: report UNKNOWN, never infer a physical lock state.
    new Characteristic::LockCurrentState(3);
    target = new Characteristic::LockTargetState(1);
  }
  boolean update() override {
    const auto result = controller.homekit(millis(), true, target->getNewVal() == 0);
    return result == door::Result::Accepted || result == door::Result::Off;
  }
  void loop() override { if (!controller.actionOn() && target->getVal() != 1) target->setVal(1); }
};
