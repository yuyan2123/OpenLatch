#pragma once
#include <cstdint>

namespace door {
constexpr uint32_t kPulseMs = 400;
constexpr uint32_t kMaxPulseMs = 800;
constexpr uint32_t kCooldownMs = 2000;
constexpr uint32_t kBootGuardMs = 3000;
constexpr int kRelayPin = 4;
constexpr int kIrPin = 3;

// Implementations must shut down independently of tick()/HomeSpan polling.
struct Relay {
  virtual ~Relay() = default;
  virtual bool start(uint32_t durationMs) = 0;
  virtual void stop() = 0;
  virtual bool active() const = 0;
};

enum class Result { Accepted, Off, Unauthorized, Offline, BootGuard,
                    Busy, Cooldown, IrDisabled, Fault };
inline const char* name(Result r) {
  switch (r) {
    case Result::Accepted: return "accepted";
    case Result::Off: return "off";
    case Result::Unauthorized: return "unauthorized";
    case Result::Offline: return "offline";
    case Result::BootGuard: return "boot_guard";
    case Result::Busy: return "busy";
    case Result::Cooldown: return "cooldown";
    case Result::IrDisabled: return "ir_disabled";
    case Result::Fault: return "fault";
  }
  return "invalid";
}

class Controller {
 public:
  explicit Controller(Relay& relay, uint32_t pulseMs = kPulseMs)
      : relay_(relay), pulseMs_(pulseMs) {}

  void boot(uint32_t now) {
    relay_.stop();
    bootAt_ = now;
    busy_ = network_ = session_ = cooling_ = false;
    fault_ = pulseMs_ == 0 || pulseMs_ > kMaxPulseMs;
  }
  void connectivity(uint32_t now, bool network, bool verifiedSession) {
    network_ = network;
    session_ = verifiedSession;
    if (!network_ || !session_) finish(now);
  }
  void tick(uint32_t now) {
    if (busy_ && (!relay_.active() || elapsed(now, startedAt_) >= pulseMs_))
      finish(now);
    if (cooling_ && elapsed(now, stoppedAt_) >= kCooldownMs) cooling_ = false;
  }
  Result homekit(uint32_t now, bool verifiedWrite, bool on) {
    tick(now);
    if (!verifiedWrite) return Result::Unauthorized;
    if (!on) { finish(now); return Result::Off; }
    if (fault_) return Result::Fault;
    if (!network_ || !session_) return Result::Offline;
    if (elapsed(now, bootAt_) < kBootGuardMs) return Result::BootGuard;
    if (busy_) return Result::Busy;
    if (cooling_) return Result::Cooldown;
    if (!relay_.start(pulseMs_)) {
      trip(now);
      return Result::Fault;
    }
    startedAt_ = now;
    busy_ = true;
    return Result::Accepted;
  }
  // Intentionally no configuration switch that enables replayable IR unlock.
  Result infrared(uint32_t now) { tick(now); return Result::IrDisabled; }
  void trip(uint32_t now) { finish(now); relay_.stop(); fault_ = true; }
  bool actionOn() const { return busy_ && relay_.active(); }
  bool faulted() const { return fault_; }

 private:
  static uint32_t elapsed(uint32_t now, uint32_t since) { return now - since; }
  void finish(uint32_t now) {
    relay_.stop();
    if (busy_) { stoppedAt_ = now; cooling_ = true; }
    busy_ = false;
  }
  Relay& relay_;
  uint32_t pulseMs_, bootAt_ = 0, startedAt_ = 0, stoppedAt_ = 0;
  bool busy_ = false, network_ = false, session_ = false;
  bool cooling_ = false, fault_ = false;
};
}  // namespace door
