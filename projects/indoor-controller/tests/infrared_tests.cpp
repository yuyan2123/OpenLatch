#include <cstdlib>
#include <iostream>
#include "../firmware/OpenLatch/IrStore.h"
#include "../firmware/OpenLatch/IrInput.h"
#include "../firmware/OpenLatch/SerialLine.h"
#include "../sim/VirtualHardware.h"
#define CHECK(x) do { if (!(x)) { std::cerr << __func__ << ':' << __LINE__ << " failed: " #x "\n"; std::exit(1); } } while (0)
using door::IrEvent;
const door::IrKey key{door::IrProtocol::Nec, 0x1234, 0x45, 32};
const door::IrKey other{door::IrProtocol::Nec, 0x1234, 0x46, 32};
void resetStorage() {
  nvsRecord.clear(); nvsPending.clear(); nvsWrites = 0;
  nvsSetupIrRecord.clear(); nvsSetupIrPending.clear();
  nvsOpenFails = nvsReadFails = nvsWriteFails = nvsCommitFails = nvsCorruptReadback = false;
}
void learn(door::Infrared& ir, uint32_t start = 0, const door::IrKey& button = key) {
  CHECK(ir.learn(start));
  CHECK(ir.receive(start + 500, button) == IrEvent::Captured);
  CHECK(ir.receive(start + 1000, button) == IrEvent::Learned);
}
void enrollment_persistence_and_forget() {
  resetStorage(); IrStore store; door::Infrared ir(store);
  CHECK(ir.begin(0)); CHECK(!ir.enabled());
  CHECK(ir.receive(500, key) == IrEvent::Ignored);
  CHECK(ir.learn(1000));
  CHECK(ir.receive(1200, key) == IrEvent::Ignored);
  CHECK(ir.receive(1700, key) == IrEvent::Captured);
  CHECK(ir.receive(1800, key, true) == IrEvent::Ignored);
  CHECK(ir.receive(2300, other) == IrEvent::Mismatch);
  CHECK(nvsWrites == 0); CHECK(!ir.enabled());
  CHECK(ir.receive(2800, key) == IrEvent::Learned);
  CHECK(nvsWrites == 1); CHECK(ir.enabled());
  CHECK(ir.receive(2900, key) == IrEvent::Ignored);
  CHECK(ir.receive(3400, key) == IrEvent::Matched);
  CHECK(ir.receive(3900, other) == IrEvent::Ignored);
  auto wrongAddress = key; ++wrongAddress.address;
  CHECK(ir.receive(4400, wrongAddress) == IrEvent::Ignored);
  auto wrongProtocol = key; wrongProtocol.protocol = door::IrProtocol::Samsung;
  CHECK(ir.receive(4900, wrongProtocol) == IrEvent::Ignored);
  auto wrongBits = key; wrongBits.bits = 31;
  CHECK(ir.receive(5400, wrongBits) == IrEvent::Ignored);
  IrStore rebootStore; door::Infrared reboot(rebootStore);
  CHECK(reboot.begin(6000)); CHECK(reboot.key() == key);
  CHECK(reboot.receive(6500, key) == IrEvent::Matched);
  CHECK(reboot.forget(7000)); CHECK(!reboot.enabled());
  CHECK(reboot.receive(7500, key) == IrEvent::Ignored);
  IrStore erasedStore; door::Infrared erased(erasedStore);
  CHECK(erased.begin(0)); CHECK(!erased.enabled());
}
void holds_cancellation_and_timeout() {
  resetStorage(); IrStore store; door::Infrared ir(store); CHECK(ir.begin(0));
  learn(ir);
  CHECK(ir.receive(1500, key) == IrEvent::Matched);
  for (uint32_t time = 1600; time < 10000; time += 100)
    CHECK(ir.receive(time, key) == IrEvent::Ignored);  // Unflagged full repeats.
  CHECK(ir.receive(10400, key, true) == IrEvent::Ignored);
  CHECK(ir.receive(10900, key, false, true) == IrEvent::Ignored);
  CHECK(ir.receive(11400, {}) == IrEvent::Ignored);
  CHECK(ir.receive(11899, key) == IrEvent::Ignored);
  CHECK(ir.receive(12399, key) == IrEvent::Matched);
  CHECK(ir.learn(13000)); CHECK(!ir.enabled());
  CHECK(ir.receive(13500, other) == IrEvent::Captured);
  ir.cancel(14000); CHECK(ir.enabled()); CHECK(ir.key() == key); CHECK(nvsWrites == 1);
  CHECK(ir.learn(15000));
  CHECK(!ir.tick(44999));
  CHECK(ir.receive(45000, key) == IrEvent::TimedOut);
  CHECK(!ir.learning()); CHECK(ir.key() == key);
  CHECK(ir.receive(45100, key) == IrEvent::Ignored);
  CHECK(ir.receive(45600, key) == IrEvent::Matched);
  CHECK(ir.learn(46000)); CHECK(ir.tick(76000)); CHECK(!ir.tick(76001));
  // Timeout arithmetic must survive millis() rollover.
  const uint32_t start = 0xfffffff0u;
  CHECK(ir.learn(start)); CHECK(!ir.tick(start + 29999)); CHECK(ir.tick(start + 30000));
  learn(ir, start, other); CHECK(ir.contains(other)); CHECK(ir.contains(key)); CHECK(ir.count() == 2);
}
void independent_setup_button_storage() {
  resetStorage();
  IrStore doorStore, setupStore("door_setup_ir");
  door::Infrared doorIr(doorStore), setupIr(setupStore);
  CHECK(doorIr.begin(0)); CHECK(setupIr.begin(0));
  learn(doorIr, 0, key); learn(setupIr, 0, other);
  IrStore rebootDoorStore, rebootSetupStore("door_setup_ir");
  door::Infrared rebootDoor(rebootDoorStore), rebootSetup(rebootSetupStore);
  CHECK(rebootDoor.begin(0)); CHECK(rebootSetup.begin(0));
  CHECK(rebootDoor.key() == key); CHECK(rebootSetup.key() == other);
  CHECK(rebootSetup.forget(500));
  IrStore unchangedDoorStore; door::Infrared unchangedDoor(unchangedDoorStore);
  CHECK(unchangedDoor.begin(0)); CHECK(unchangedDoor.key() == key);
  nvsSetupIrRecord[0] ^= 1;
  IrStore damagedSetupStore("door_setup_ir"); door::Infrared damagedSetup(damagedSetupStore);
  CHECK(!damagedSetup.begin(0)); CHECK(!damagedSetup.enabled());
  CHECK(unchangedDoor.enabled()); CHECK(damagedSetup.forget(500));
}
void multiple_buttons_and_capacity() {
  resetStorage(); IrStore store; door::Infrared ir(store); CHECK(ir.begin(0));
  uint32_t now = 1000;
  for (uint16_t command = 0; command < door::kIrMaxKeys; ++command) {
    learn(ir, now, {door::IrProtocol::Nec, 0x4567, command, 32}); now += 1500;
    CHECK(ir.count() == command + 1);
  }
  CHECK(!ir.learn(now)); CHECK(ir.count() == door::kIrMaxKeys);
  for (uint16_t command = 0; command < door::kIrMaxKeys; ++command) {
    CHECK(ir.receive(now, {door::IrProtocol::Nec, 0x4567, command, 32}) == IrEvent::Matched); now += 500;
  }
  CHECK(!ir.remove(now, door::kIrMaxKeys)); CHECK(ir.count() == door::kIrMaxKeys);
  CHECK(ir.remove(now, 4)); CHECK(ir.count() == door::kIrMaxKeys - 1);
  CHECK(!ir.contains({door::IrProtocol::Nec, 0x4567, 4, 32}));
  CHECK(!ir.remove(now, 4));
  const int writes = nvsWrites;
  CHECK(ir.learn(now));
  CHECK(ir.receive(now + 500, ir.at(0)) == IrEvent::Captured);
  CHECK(ir.receive(now + 1000, ir.at(0)) == IrEvent::Duplicate);
  CHECK(nvsWrites == writes); CHECK(ir.count() == door::kIrMaxKeys - 1); CHECK(!ir.learning());
  now += 1500;
  const door::IrKey replacement{door::IrProtocol::Sony, 9, 99, 12};
  learn(ir, now, replacement); CHECK(ir.at(4) == replacement);
  IrStore rebootStore; door::Infrared reboot(rebootStore); CHECK(reboot.begin(0));
  CHECK(reboot.count() == door::kIrMaxKeys); CHECK(reboot.contains(replacement));
  CHECK(reboot.remove(500, 0)); CHECK(reboot.contains(replacement));
  CHECK(reboot.forget(1000)); CHECK(reboot.count() == 0); CHECK(!reboot.enabled());
  IrStore emptyStore; door::Infrared empty(emptyStore); CHECK(empty.begin(0)); CHECK(empty.count() == 0);
}
void legacy_single_key_migration() {
  resetStorage();
  // Actual v1 layout for NEC 0x1234 / 0x45 / 32, with its original CRC32.
  const std::vector<uint8_t> legacy{0x49,0x52,0x01,0x01,0x01,0x00,0x34,0x12,
                                    0x45,0x00,0x20,0x00,0xfc,0x5d,0x5b,0x7e};
  nvsRecord = legacy; nvsSetupIrRecord = legacy;
  IrStore store, setupStore("door_setup_ir"); door::Infrared ir(store), setupIr(setupStore);
  CHECK(ir.begin(0)); CHECK(ir.count() == 1); CHECK(ir.key() == key);
  CHECK(setupIr.begin(0)); CHECK(setupIr.count() == 1); CHECK(setupIr.key() == key);
  CHECK(nvsWrites == 0); CHECK(nvsRecord == legacy); CHECK(nvsSetupIrRecord == legacy);
  learn(ir, 1000, other); CHECK(ir.count() == 2); CHECK(ir.contains(key)); CHECK(ir.contains(other));
  CHECK(nvsRecord.size() == 136); CHECK(nvsRecord[2] == 2); CHECK(nvsSetupIrRecord == legacy);
  IrStore rebootStore; door::Infrared reboot(rebootStore); CHECK(reboot.begin(0));
  CHECK(reboot.count() == 2); CHECK(reboot.contains(key)); CHECK(reboot.contains(other));
  CHECK(reboot.remove(1000, 0));
  IrStore afterRemoval; door::Infrared remaining(afterRemoval); CHECK(remaining.begin(0));
  CHECK(remaining.count() == 1); CHECK(remaining.key() == other);
  resetStorage();
  nvsRecord = {0x49,0x52,0x01,0x00,0,0,0,0,0,0,0,0,0xd5,0x44,0x0f,0x19};
  IrStore tombstone; door::Infrared disabled(tombstone);
  CHECK(disabled.begin(0)); CHECK(disabled.count() == 0); CHECK(!disabled.enabled());
  learn(disabled, 1000); CHECK(disabled.count() == 1);
}
void removal_storage_failure() {
  for (int mode = 0; mode < 3; ++mode) {
    resetStorage(); IrStore store; door::Infrared ir(store); CHECK(ir.begin(0));
    learn(ir, 1000, key); learn(ir, 2500, other); CHECK(ir.count() == 2);
    nvsWriteFails = mode == 0; nvsCommitFails = mode == 1; nvsCorruptReadback = mode == 2;
    CHECK(!ir.remove(4000, 0)); CHECK(!ir.ready()); CHECK(!ir.enabled());
    CHECK(ir.receive(4500, other) == IrEvent::Ignored);
    nvsWriteFails = nvsCommitFails = nvsCorruptReadback = false;
    CHECK(ir.forget(5000)); CHECK(ir.ready()); CHECK(ir.count() == 0);
  }
}
void storage_failures() {
  resetStorage(); nvsOpenFails = true; IrStore unopened; door::Infrared unavailable(unopened);
  CHECK(!unavailable.begin(0)); CHECK(!unavailable.learn(0));
  CHECK(unavailable.receive(500, key) == IrEvent::Ignored);
  for (int mode = 0; mode < 3; ++mode) {
    resetStorage(); IrStore store; door::Infrared ir(store); CHECK(ir.begin(0)); learn(ir);
    CHECK(ir.learn(2000)); CHECK(ir.receive(2500, other) == IrEvent::Captured);
    nvsWriteFails = mode == 0; nvsCommitFails = mode == 1; nvsCorruptReadback = mode == 2;
    CHECK(ir.receive(3000, other) == IrEvent::StorageError);
    CHECK(!ir.enabled()); CHECK(!ir.ready()); CHECK(!ir.learning());
    CHECK(ir.receive(3500, key) == IrEvent::Ignored);
  }
  resetStorage(); IrStore store; door::Infrared ir(store); CHECK(ir.begin(0)); learn(ir);
  const auto goodRecord = nvsRecord;
  for (size_t byte = 0; byte < goodRecord.size(); ++byte) {
    nvsRecord = goodRecord; nvsRecord[byte] ^= 1;
    IrStore corrupted; door::Infrared loaded(corrupted);
    CHECK(!loaded.begin(0)); CHECK(!loaded.enabled());
  }
  nvsRecord = goodRecord; nvsRecord.pop_back();
  IrStore truncated; door::IrKeys loaded;
  CHECK(truncated.load(loaded) == door::IrLoad::Error);
  nvsRecord = goodRecord; nvsRecord.push_back(0);
  IrStore oversized; CHECK(oversized.load(loaded) == door::IrLoad::Error);
  nvsRecord = goodRecord; nvsReadFails = true;
  IrStore unreadable; CHECK(unreadable.load(loaded) == door::IrLoad::Error);
  nvsReadFails = false; nvsWriteFails = true;
  CHECK(!ir.forget(2000)); CHECK(!ir.enabled()); CHECK(!ir.learn(2000));
  nvsWriteFails = false;
  CHECK(ir.forget(2500)); CHECK(ir.ready()); CHECK(!ir.enabled());
  nvsRecord = goodRecord; nvsRecord[0] ^= 1;
  IrStore damaged; door::Infrared repairable(damaged);
  CHECK(!repairable.begin(0)); CHECK(repairable.forget(500));
  CHECK(repairable.ready()); CHECK(!repairable.enabled());
  learn(repairable, 1000);
}
void decoding_and_serial_input() {
  IRData frame{NEC, key.address, key.command, key.bits, 0};
  CHECK(irKey(frame) == key); frame.protocol = NEC2; CHECK(irKey(frame) == key);
  for (auto protocol : {SONY, SAMSUNG, SAMSUNGLG, RC5, RC6, JVC}) {
    frame.protocol = protocol; CHECK(irKey(frame).valid());
  }
  for (auto protocol : {UNKNOWN, PULSE_DISTANCE, PULSE_WIDTH, RC6A, KASEIKYO, SAMSUNG48}) {
    frame.protocol = protocol; CHECK(!irKey(frame).valid());
  }
  frame.protocol = RC5; frame.flags = IRDATA_FLAGS_TOGGLE_BIT;
  CHECK(!irRepeat(frame)); CHECK(!irInvalid(frame));
  for (auto flag : {IRDATA_FLAGS_WAS_OVERFLOW, IRDATA_FLAGS_PARITY_FAILED, IRDATA_FLAGS_EXTRA_INFO}) {
    frame.flags = flag; CHECK(irInvalid(frame));
  }
  frame.flags = IRDATA_FLAGS_IS_AUTO_REPEAT; CHECK(irRepeat(frame));
  frame.flags = IRDATA_FLAGS_IS_REPEAT; CHECK(irRepeat(frame));
  SerialLine line;
  for (char c : std::string("ir learn")) CHECK(line.feed(c) == SerialLine::Result::Pending);
  CHECK(line.feed('\r') == SerialLine::Result::Complete);
  CHECK(std::string(line.value()) == "ir learn");
  CHECK(line.feed('\n') == SerialLine::Result::Pending);
  for (int i = 0; i < 80; ++i) line.feed('x');
  CHECK(line.feed('\n') == SerialLine::Result::Rejected);
  for (char c : std::string("ir forget")) line.feed(c);
  line.feed('\0'); CHECK(line.feed('\n') == SerialLine::Result::Rejected);
  for (char c : std::string("ir status")) line.feed(c);
  CHECK(line.feed('\n') == SerialLine::Result::Complete);
  CHECK(std::string(line.value()) == "ir status");
}
void learned_key_controls_shared_relay() {
  resetStorage(); IrStore store; door::Infrared ir(store); CHECK(ir.begin(0));
  Rig r; r.advance(3000);
  CHECK(ir.learn(r.now())); r.controller.inhibit(r.now(), ir.learning());
  r.network(true, true); CHECK(r.on() == door::Result::Inhibited);
  r.advance(500); CHECK(ir.receive(r.now(), key) == IrEvent::Captured);
  r.advance(500); CHECK(ir.receive(r.now(), key) == IrEvent::Learned);
  CHECK(r.relay.trace.empty());  // Neither learning press actuates the relay.
  r.controller.inhibit(r.now(), ir.learning());
  r.advance(500); CHECK(ir.receive(r.now(), key) == IrEvent::Matched);
  CHECK(r.controller.infrared(r.now(), true) == door::Result::Accepted);
  CHECK(r.on() == door::Result::Busy);
  for (int i = 0; i < 50; ++i) {
    r.advance(100);
    CHECK(ir.receive(r.now(), key) == IrEvent::Ignored);
  }
  CHECK(r.relay.trace.size() == 2);  // Holding past cooldown does not reopen.
  CHECK(r.on() == door::Result::Accepted);
  r.advance(500); CHECK(ir.receive(r.now(), key) == IrEvent::Matched);
  CHECK(r.controller.infrared(r.now(), true) == door::Result::Cooldown);
  r.advance(2000); r.network(false, false);
  CHECK(ir.receive(r.now(), key) == IrEvent::Matched);
  CHECK(r.controller.infrared(r.now(), true) == door::Result::Accepted);
  r.network(false, false); CHECK(r.relay.high);
  r.advance(400); CHECK(!r.relay.high);
}
int main() {
  enrollment_persistence_and_forget(); holds_cancellation_and_timeout(); independent_setup_button_storage();
  multiple_buttons_and_capacity(); legacy_single_key_migration(); removal_storage_failure();
  storage_failures(); decoding_and_serial_input(); learned_key_controls_shared_relay();
  std::cout << "PASS: infrared enrollment, actual NVS adapter, repeat filtering, USB parsing and shared relay.\n";
}
