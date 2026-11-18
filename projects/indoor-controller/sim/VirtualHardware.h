#pragma once
#include <cstdint>
#include <vector>
#include "../firmware/OpenLatch/Controller.h"

struct Transition { uint64_t at; bool on; };
// Event-driven virtual timer. Advancing time also runs the timer even when
// the controller's main loop is intentionally stalled.
class VirtualRelay : public door::Relay {
 public:
  uint64_t now = 0, deadline = 0;
  bool high = false, failStart = false;
  std::vector<Transition> trace;
  bool start(uint32_t ms, door::Source = door::Source::HomeKit) override {
    if (failStart || !ms || ms > door::kMaxPulseMs) return false;
    high = true;
    deadline = now + ms;
    trace.push_back({now, true});
    return true;
  }
  void stop() override {
    if (high) trace.push_back({now, false});
    high = false;
  }
  bool active() const override { return high; }
  void advance(uint64_t ms) {
    const uint64_t target = now + ms;
    if (high && target >= deadline) {
      now = deadline;
      stop();
    }
    now = target;
  }
};

struct Rig {
  VirtualRelay relay;
  door::Controller controller;
  bool wifi = false, session = false;
  explicit Rig(uint32_t pulseMs = door::kPulseMs) : controller(relay, pulseMs) {
    controller.boot(0);
  }
  uint32_t now() const { return uint32_t(relay.now); }
  void network(bool w, bool s) {
    wifi = w; session = s;
    controller.connectivity(now(), w, s);
  }
  void advance(uint64_t ms, bool poll = true) {
    relay.advance(ms);
    if (poll) controller.tick(now());
  }
  door::Result on(bool authorized = true) {
    return controller.homekit(now(), authorized, true);
  }
  void ready() { advance(door::kBootGuardMs); network(true, true); }
  void reboot() { controller.boot(now()); wifi = session = false; }
};
