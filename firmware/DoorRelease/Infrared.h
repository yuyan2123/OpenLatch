#pragma once
#include <cstdint>

namespace door {
constexpr uint32_t kIrQuietMs = 500;
constexpr uint32_t kIrLearnMs = 30000;

// Stable storage IDs, independent of the IRremote library's enum ordering.
enum class IrProtocol : uint16_t { None, Nec, Sony, Samsung, Rc5, Rc6, Jvc };
inline const char* name(IrProtocol protocol) {
  switch (protocol) {
    case IrProtocol::None: return "none";
    case IrProtocol::Nec: return "NEC";
    case IrProtocol::Sony: return "Sony";
    case IrProtocol::Samsung: return "Samsung";
    case IrProtocol::Rc5: return "RC5";
    case IrProtocol::Rc6: return "RC6";
    case IrProtocol::Jvc: return "JVC";
  }
  return "invalid";
}
struct IrKey {
  IrProtocol protocol = IrProtocol::None;
  uint16_t address = 0, command = 0, bits = 0;
  bool valid() const {
    return protocol >= IrProtocol::Nec && protocol <= IrProtocol::Jvc && bits > 0 && bits <= 64;
  }
  bool operator==(const IrKey& other) const {
    return protocol == other.protocol && address == other.address &&
           command == other.command && bits == other.bits;
  }
};
enum class IrLoad { Empty, Loaded, Error };
struct IrStorage {
  virtual ~IrStorage() = default;
  virtual IrLoad load(IrKey& key) = 0;
  // An invalid key writes a disabled record. Each update must be atomic.
  virtual bool save(const IrKey& key) = 0;
};
enum class IrEvent { Ignored, Captured, Mismatch, Learned, Matched, StorageError, TimedOut };
inline const char* name(IrEvent event) {
  switch (event) {
    case IrEvent::Ignored: return "ignored";
    case IrEvent::Captured: return "captured";
    case IrEvent::Mismatch: return "mismatch";
    case IrEvent::Learned: return "learned";
    case IrEvent::Matched: return "matched";
    case IrEvent::StorageError: return "storage_error";
    case IrEvent::TimedOut: return "timed_out";
  }
  return "invalid";
}

class Infrared {
 public:
  explicit Infrared(IrStorage& storage) : storage_(storage) {}
  bool begin(uint32_t now) {
    key_ = candidate_ = {};
    learning_ = false;
    lastActivity_ = now;
    const auto result = storage_.load(key_);
    ready_ = result != IrLoad::Error && (result != IrLoad::Loaded || key_.valid());
    if (result != IrLoad::Loaded || !ready_) key_ = {};
    return ready_;
  }
  bool learn(uint32_t now) {
    if (!ready_) return false;
    learning_ = true;
    candidate_ = {};
    startedAt_ = lastActivity_ = now;
    return true;
  }
  void cancel(uint32_t now) {
    learning_ = false;
    candidate_ = {};
    lastActivity_ = now;
  }
  bool forget(uint32_t now) {
    cancel(now);
    key_ = {};
    // A verified tombstone also repairs a corrupt credential without touching
    // Wi-Fi or HomeKit pairing data in other NVS namespaces.
    ready_ = storage_.save(key_);
    return ready_;
  }
  bool tick(uint32_t now) {
    if (!learning_ || uint32_t(now - startedAt_) < kIrLearnMs) return false;
    cancel(now);
    return true;
  }
  // Every decoded frame, including repeats/noise, updates the quiet interval.
  // A held button cannot rearm even if its protocol sends full repeat frames.
  IrEvent receive(uint32_t now, const IrKey& input, bool repeat = false, bool invalid = false) {
    const bool expired = tick(now);
    const bool quiet = uint32_t(now - lastActivity_) >= kIrQuietMs;
    lastActivity_ = now;
    if (expired) return IrEvent::TimedOut;
    if (!ready_ || invalid || repeat || !quiet || !input.valid()) return IrEvent::Ignored;
    if (learning_) {
      if (!candidate_.valid()) {
        candidate_ = input;
        return IrEvent::Captured;
      }
      if (!(input == candidate_)) return IrEvent::Mismatch;
      // Persist before enabling the new key; never unlock on a learning frame.
      if (!storage_.save(candidate_)) {
        ready_ = false;
        key_ = {};
        cancel(now);
        return IrEvent::StorageError;
      }
      key_ = candidate_;
      cancel(now);
      return IrEvent::Learned;
    }
    return enabled() && input == key_ ? IrEvent::Matched : IrEvent::Ignored;
  }
  bool ready() const { return ready_; }
  bool learning() const { return learning_; }
  bool enabled() const { return ready_ && !learning_ && key_.valid(); }
  const IrKey& key() const { return key_; }

 private:
  IrStorage& storage_;
  IrKey key_{}, candidate_{};
  uint32_t startedAt_ = 0, lastActivity_ = 0;
  bool ready_ = false, learning_ = false;
};
}  // namespace door
