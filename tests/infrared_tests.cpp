#include <cstdlib>
#include <iostream>
#include "../firmware/DoorRelease/IrStore.h"
#include "../firmware/DoorRelease/IrInput.h"
#include "../firmware/DoorRelease/SerialLine.h"
#include "../sim/VirtualHardware.h"
#define CHECK(x) do { if (!(x)) { std::cerr << __func__ << ':' << __LINE__ << " failed: " #x "\n"; std::exit(1); } } while (0)
using door::IrEvent;
const door::IrKey key{door::IrProtocol::Nec, 0x1234, 0x45, 32};
const door::IrKey other{door::IrProtocol::Nec, 0x1234, 0x46, 32};
void resetStorage() {
  nvsRecord.clear(); nvsPending.clear(); nvsWrites = 0;
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
  learn(ir, start, other); CHECK(ir.key() == other);
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
  IrStore truncated; door::IrKey loaded;
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
  enrollment_persistence_and_forget(); holds_cancellation_and_timeout();
  storage_failures(); decoding_and_serial_input(); learned_key_controls_shared_relay();
  std::cout << "PASS: infrared enrollment, actual NVS adapter, repeat filtering, USB parsing and shared relay.\n";
}
