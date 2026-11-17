#include <cstdlib>
#include <iostream>
#include <random>
#include "../sim/VirtualHardware.h"

#define CHECK(x) do { if (!(x)) { std::cerr << __func__ << ':' << __LINE__ << " failed: " #x "\n"; std::exit(1); } } while (0)
using door::Result;
void boot_and_auth() {
  Rig r;
  CHECK(!r.relay.high);
  CHECK(r.on() == Result::Offline);
  r.network(true, true);
  CHECK(r.on() == Result::BootGuard);
  r.advance(2999);
  CHECK(r.on() == Result::BootGuard);
  r.advance(1);
  CHECK(r.on(false) == Result::Unauthorized);
  CHECK(!r.relay.high);
  CHECK(r.on() == Result::Accepted);
}
void pulse_and_duplicates() {
  Rig r; r.ready();
  CHECK(r.on() == Result::Accepted);
  r.advance(100); CHECK(r.on() == Result::Busy);
  r.advance(99); CHECK(r.relay.high);
  r.advance(1); CHECK(!r.relay.high);
  CHECK(!r.controller.actionOn());
  CHECK(r.relay.trace.size() == 2);
  CHECK(r.relay.trace[1].at - r.relay.trace[0].at == 200);
  CHECK(r.on() == Result::Cooldown);
  r.advance(1999); CHECK(r.on() == Result::Cooldown);
  r.advance(1); CHECK(r.on() == Result::Accepted);
}
void independent_deadline() {
  Rig r; r.ready(); CHECK(r.on() == Result::Accepted);
  r.advance(10000, false);  // Main loop never polls during the entire pulse.
  CHECK(!r.relay.high);
  CHECK(r.relay.trace.back().at == 3200);
  CHECK(r.relay.now == 13000);
  r.controller.tick(r.now());
  CHECK(!r.controller.actionOn());
}
void disconnection_and_no_queue() {
  for (int mode = 0; mode < 2; ++mode) {
    Rig r; r.ready(); CHECK(r.on() == Result::Accepted);
    r.advance(99); r.network(mode == 0, false);
    CHECK(!r.relay.high);
    CHECK(r.on() == Result::Offline);
    r.network(true, true);  // A fast reconnect must not revive the pulse.
    CHECK(!r.relay.high);
    CHECK(r.on() == Result::Cooldown);
    r.network(mode == 0, false);
    r.advance(3000); r.network(true, true);
    CHECK(!r.relay.high);
    CHECK(r.relay.trace.size() == 2);
    CHECK(r.on() == Result::Accepted);
  }
}
void reboot_and_fault() {
  Rig r; r.ready(); r.on(); r.advance(10); r.reboot();
  CHECK(!r.relay.high); CHECK(r.on() == Result::Offline);
  r.network(true, true); CHECK(r.on() == Result::BootGuard);
  r.advance(3000); CHECK(r.on() == Result::Accepted);
  r.controller.trip(r.now()); CHECK(!r.relay.high);
  r.advance(5000); CHECK(r.on() == Result::Fault);
  r.reboot(); r.advance(3000); r.network(true, true);
  CHECK(r.on() == Result::Accepted);
}
void off_and_ir() {
  Rig r; r.ready();
  for (int i = 0; i < 100; ++i) CHECK(r.controller.infrared(r.now()) == Result::IrDisabled);
  CHECK(r.relay.trace.empty());
  r.on(); r.advance(100);
  CHECK(r.controller.homekit(r.now(), false, false) == Result::Unauthorized);
  CHECK(r.relay.high);
  CHECK(r.controller.homekit(r.now(), true, false) == Result::Off);
  CHECK(!r.relay.high); CHECK(r.on() == Result::Cooldown);
}
void limits_and_driver_failure() {
  for (auto ms : {0u, 801u, 0xffffffffu}) {
    Rig r(ms); r.ready(); CHECK(r.on() == Result::Fault); CHECK(!r.relay.high);
  }
  Rig r(800); r.ready(); CHECK(r.on() == Result::Accepted);
  r.advance(799, false); CHECK(r.relay.high);
  r.advance(1, false); CHECK(!r.relay.high);
  Rig failed; failed.ready(); failed.relay.failStart = true;
  CHECK(failed.on() == Result::Fault); CHECK(!failed.relay.high);
  failed.relay.failStart = false; CHECK(failed.on() == Result::Fault);
}
void infrared_and_homekit_coexistence() {
  Rig r;
  CHECK(r.controller.infrared(r.now(), true) == Result::BootGuard);
  r.advance(3000);
  CHECK(r.on() == Result::Offline);
  CHECK(r.controller.infrared(r.now(), true) == Result::Accepted);
  r.network(false, false);  // Offline synchronization must preserve a local pulse.
  r.advance(100);
  CHECK(r.relay.high);
  r.network(true, true);
  CHECK(r.on() == Result::Busy);
  CHECK(r.controller.infrared(r.now(), true) == Result::Busy);
  r.advance(300);
  CHECK(!r.relay.high);
  CHECK(r.on() == Result::Cooldown);
  CHECK(r.controller.infrared(r.now(), true) == Result::Cooldown);
  r.advance(2000);
  CHECK(r.on() == Result::Accepted);
  CHECK(r.controller.infrared(r.now(), true) == Result::Busy);
  r.network(false, false);  // HomeKit pulses still stop immediately on loss.
  CHECK(!r.relay.high);
  CHECK(r.controller.infrared(r.now(), true) == Result::Cooldown);
  r.advance(2000);
  CHECK(r.controller.infrared(r.now(), true) == Result::Accepted);
  r.network(true, true);
  CHECK(r.controller.homekit(r.now(), false, false) == Result::Unauthorized);
  CHECK(r.relay.high);
  CHECK(r.controller.homekit(r.now(), true, false) == Result::Off);
  CHECK(!r.relay.high);
  r.advance(2000);
  r.controller.inhibit(r.now(), true);
  CHECK(r.on() == Result::Inhibited);
  CHECK(r.controller.infrared(r.now(), true) == Result::Inhibited);
  r.controller.inhibit(r.now(), false);
  CHECK(r.controller.infrared(r.now(), true) == Result::Accepted);
  r.controller.inhibit(r.now(), true);  // Enrollment interrupts either source.
  CHECK(!r.relay.high);
  r.controller.inhibit(r.now(), false);
  CHECK(r.on() == Result::Cooldown);
  r.advance(2000);
  CHECK(r.on() == Result::Accepted);
  r.controller.inhibit(r.now(), true);
  CHECK(!r.relay.high);
  r.controller.inhibit(r.now(), false);
  r.advance(2000);
  CHECK(r.controller.infrared(r.now()) == Result::IrDisabled);
  CHECK(!r.relay.high);
  CHECK(r.controller.infrared(r.now(), true) == Result::Accepted);
  r.advance(10000, false);
  CHECK(!r.relay.high);
  CHECK(r.relay.trace.back().at - r.relay.trace[r.relay.trace.size() - 2].at == 200);
  r.controller.trip(r.now());
  CHECK(r.controller.infrared(r.now(), true) == Result::Fault);
}
void millis_wraparound() {
  Rig r;
  r.advance(0xffffffffULL - 1000); r.reboot();
  r.network(true, true); r.advance(2999);
  CHECK(r.on() == Result::BootGuard);
  r.advance(1); CHECK(r.on() == Result::Accepted);
  r.advance(200); CHECK(!r.relay.high);
  r.advance(2000); CHECK(r.on() == Result::Accepted);
  // Pulse itself straddles uint32_t millis rollover.
  Rig p; p.ready(); p.advance(0xffffffffULL - 100 - p.relay.now);
  CHECK(p.on() == Result::Accepted);
  p.advance(199); CHECK(p.relay.high);
  p.advance(1); CHECK(!p.relay.high);
}
void randomized_invariants() {
  Rig r; r.ready();
  std::mt19937 random(20261112);
  for (int i = 0; i < 20000; ++i) {
    switch (random() % 8) {
      case 0: r.on(); break;
      case 1: r.on(false); break;
      case 2: r.controller.infrared(r.now()); break;
      case 3: r.network(false, false); break;
      case 4: r.network(true, true); break;
      case 5: r.reboot(); break;
      case 6: r.controller.homekit(r.now(), true, false); break;
      case 7: r.advance(random() % 5000, false); break;
    }
    r.advance(random() % 200);
    if (!r.wifi || !r.session || r.controller.faulted()) CHECK(!r.relay.high);
  }
  r.advance(1000);
  CHECK(r.relay.trace.size() % 2 == 0);
  for (size_t i = 0; i < r.relay.trace.size(); i += 2) {
    CHECK(r.relay.trace[i].on);
    CHECK(!r.relay.trace[i+1].on);
    CHECK(r.relay.trace[i+1].at - r.relay.trace[i].at <= 200);
  }
}
void configured_duration() {
  for (uint32_t ms : {1u, 375u, 800u}) {
    for (bool ir : {false, true}) {
      Rig r; r.controller.boot(0, ms); r.ready();
      CHECK((ir ? r.controller.infrared(r.now(), true) : r.on()) == Result::Accepted);
      r.advance(ms - 1, false); CHECK(r.relay.high);
      CHECK(r.on() == Result::Busy);
      r.advance(1, false); CHECK(!r.relay.high);
      CHECK(r.relay.trace.back().at == door::kBootGuardMs + ms);
      CHECK(r.on() == Result::Cooldown);
      r.advance(door::kCooldownMs); CHECK(r.on() == Result::Accepted);
    }
  }
  Rig bad; bad.controller.boot(0, 801); bad.ready(); CHECK(bad.on() == Result::Fault);
  CHECK(!bad.relay.high);
}
void nfc_permissions_and_coexistence() {
  Rig r; r.ready(); CHECK(r.controller.nfc(r.now()) == Result::Unauthorized);
  CHECK(r.controller.nfc(r.now(), true) == Result::Accepted);
  CHECK(r.on() == Result::Busy);
  r.network(false, false); CHECK(r.relay.high);  // A locally verified NFC pulse is independent of HAP connectivity.
  r.advance(200); CHECK(!r.relay.high);
  CHECK(r.controller.nfc(r.now(), true) == Result::Cooldown);
  r.advance(2000); r.controller.inhibit(r.now(), true);
  CHECK(r.controller.nfc(r.now(), true) == Result::Inhibited);
  r.controller.inhibit(r.now(), false); CHECK(r.controller.infrared(r.now(), true) == Result::Accepted);
}
int main() {
  boot_and_auth(); pulse_and_duplicates(); independent_deadline();
  disconnection_and_no_queue(); reboot_and_fault(); off_and_ir();
  limits_and_driver_failure(); millis_wraparound(); randomized_invariants();
  infrared_and_homekit_coexistence(); configured_duration(); nfc_permissions_and_coexistence();
  std::cout << "PASS: 10 controller scenario groups, including 20,000 randomized events.\n";
}
