#pragma once
#include "Protocol.h"
#include <limits>

namespace outdoor {
constexpr uint32_t kLeaseMs = 5000, kTapMs = 5000, kIdleMs = 1500;
constexpr uint32_t kTagRetryMs = 150, kReleaseQuietMs = 500;
enum class ExchangeResult { Ok, Failed, RetryableRfError };
struct Reader {
  virtual ~Reader() = default;
  virtual bool configure(const std::array<uint8_t, 8>& group) = 0;
  virtual ExchangeResult exchange(const uint8_t* command, size_t length, uint8_t* response, size_t& size) = 0;
  virtual void release() = 0;
  virtual void endTap(bool) { release(); }
  virtual bool restartField() { return false; }
};
struct Output {
  virtual ~Output() = default;
  virtual void send(const Frame&) = 0;
  virtual Nonce randomNonce() = 0;
};

// One worker owns this object and the PN532. Network callbacks only enqueue.
// This is a transport state machine, never a credential verifier or lock controller.
class ReaderSession {
 public:
  ReaderSession(Reader& reader, Output& output) : reader_(reader), output_(output) {}
  void begin(uint32_t now) { reset(now); }
  bool bound() const { return bound_; }
  bool active() const { return active_; }
  bool rearming() const { return rearming_; }
  const Nonce& nonce() const { return outdoor_; }
  void hello() {
    if (bound_) return;
    Frame f = make(Type::Hello);
    if (f.seq) output_.send(f);
  }
  void tick(uint32_t now) {
    if (tx_ >= std::numeric_limits<uint32_t>::max() - 16) { reset(now); return; }
    if (bound_ && elapsed(now, leaseAt_) >= kLeaseMs) { reset(now); return; }
    if (active_ && (elapsed(now, tapAt_) >= kTapMs || elapsed(now, commandAt_) >= kIdleMs)) {
      endTap(now);
      cached_ = false;
    }
    if (active_ && expectedRequest_ == 1 && elapsed(now, tagSentAt_) >= kTagRetryMs) {
      tagSentAt_ = now;
      output_.send(tag_);
    }
  }
  // An ISO-DEP RF error may trigger one fresh activation while the phone is still present.
  bool prepareRetry(uint32_t now) {
    if (retry_ == Retry::Pending) {
      retryAt_ = now;
      retry_ = reader_.restartField() ? Retry::Probing : Retry::None;
    }
    if (retry_ == Retry::Probing && elapsed(now, retryAt_) >= kRfRetryMs) retry_ = Retry::None;
    return retry_ == Retry::Probing;
  }
  // After normal completion or an exhausted retry, wait for a continuous quiet period.
  void presence(uint32_t now, bool present) {
    if (!rearming_ || retry_ != Retry::None) return;
    if (present) { quiet_ = false; return; }
    if (!quiet_) { quiet_ = true; quietAt_ = now; }
    if (elapsed(now, quietAt_) >= kReleaseQuietMs) { rearming_ = quiet_ = false; }
  }
  bool detected(uint32_t now, const uint8_t* uid, size_t length,
                std::array<uint8_t, 2> atqa, uint8_t sak) {
    tick(now);
    const bool retry = retry_ == Retry::Probing && elapsed(now, retryAt_) < kRfRetryMs;
    if (!bound_ || active_ || (rearming_ && !retry) || !uid || !length || length > 10) return false;
    if (retry && !(sak & kIsoDep)) { retry_ = Retry::None; return false; }
    if (tapCounter_ == std::numeric_limits<uint32_t>::max()) { reset(now); return false; }
    retried_ = retry; retry_ = Retry::None;
    rearming_ = quiet_ = false;
    active_ = true;
    tap_ = ++tapCounter_; expectedRequest_ = 1;
    tapAt_ = commandAt_ = tagSentAt_ = now;
    // Unsolicited Tag must not discard a Configure/Ping/Release response
    // that the indoor peer may still be retrying after packet loss.
    tag_ = make(Type::Tag);
    tag_.tap = tap_; tag_.length = uint16_t(length + 4);
    tag_.payload[0] = uint8_t(length);
    std::copy_n(uid, length, tag_.payload.begin() + 1);
    tag_.payload[length + 1] = atqa[0]; tag_.payload[length + 2] = atqa[1];
    tag_.payload[length + 3] = sak;
    if (tag_.seq) output_.send(tag_);
    return true;
  }
  // Called only after Codec verifies the HMAC and radio addresses are matched.
  void receive(uint32_t now, const Frame& f) {
    tick(now);
    if (!fromIndoor(f.type) || f.code != Code::Ok || f.outdoor != outdoor_ || !f.seq || f.length > kMaxPayload) return;
    if (bound_ && f.indoor != indoor_) return;
    if (cached_ && f.seq == lastRx_ && same(f, lastInput_)) {
      // A retry resends the exact cached result without repeating the APDU.
      output_.send(lastOutput_);
      return;
    }
    if (f.seq <= lastRx_) return;
    if (!bound_) {
      if (f.type != Type::Configure || f.seq != 1 || f.tap || f.request ||
          f.length != 8 || !nonzero(f.indoor) || !nonzero(outdoor_)) return;
      std::array<uint8_t, 8> group{};
      std::copy_n(f.payload.begin(), 8, group.begin());
      if (!reader_.configure(group)) { reset(now); return; }
      indoor_ = f.indoor; bound_ = true; leaseAt_ = now;
      respond(f, Type::Ack, Code::Ok);
      return;
    }
    // A live lease cannot be replaced by another Configure. Reconnect gets a new nonce.
    if (f.type == Type::Configure) return;
    if (f.type == Type::Ping) {
      if (f.length || f.tap || f.request) return;
      leaseAt_ = now;
      respond(f, Type::Ack, Code::Ok);
    } else if (f.type == Type::Release) {
      if (f.length || !active_ || f.tap != tap_ || f.request) return;
      leaseAt_ = now;
      endTap(now);
      respond(f, Type::Ack, Code::Ok);
    } else if (f.type == Type::Exchange) {
      if (!active_ || f.tap != tap_ || f.request != expectedRequest_ ||
          f.length < 4 || f.length > kMaxCommand) return;
      leaseAt_ = commandAt_ = now;
      Frame result = make(Type::Response);
      result.tap = tap_; result.request = f.request;
      size_t size = kMaxPayload;
      const auto exchange = reader_.exchange(f.payload.data(), f.length, result.payload.data(), size);
      if (exchange != ExchangeResult::Ok ||
          size < 2 || size > kMaxPayload) {
        result.code = Code::ReaderError;
        size = 0;
        endTap(now);
        if (exchange == ExchangeResult::RetryableRfError && !retried_ && (tag_.payload[tag_.length - 1] & kIsoDep))
          retry_ = Retry::Pending;
      } else {
        ++expectedRequest_;
      }
      result.length = uint16_t(size);
      cache(f, result);
    }
  }
 private:
  enum class Retry { None, Pending, Probing };
  static constexpr uint8_t kIsoDep = 0x20;
  static constexpr uint32_t kRfRetryMs = 1200;
  static uint32_t elapsed(uint32_t now, uint32_t since) { return now - since; }
  Frame make(Type type) {
    Frame f;
    f.type = type; f.outdoor = outdoor_; f.indoor = indoor_;
    // Exhaustion requires a fresh epoch; never wrap a sequence number.
    if (tx_ != std::numeric_limits<uint32_t>::max()) f.seq = ++tx_;
    return f;
  }
  void endTap(uint32_t now) {
    reader_.endTap((tag_.payload[tag_.length - 1] & kIsoDep) != 0); active_ = false;
    retry_ = Retry::None;
    rearming_ = true; quiet_ = false; quietAt_ = now;
  }
  void reset(uint32_t now) {
    if (active_) reader_.endTap((tag_.payload[tag_.length - 1] & kIsoDep) != 0);
    outdoor_ = output_.randomNonce(); indoor_ = {};
    bound_ = active_ = cached_ = false;
    retried_ = false; retry_ = Retry::None;
    rearming_ = true; quiet_ = false;
    tx_ = lastRx_ = tapCounter_ = tap_ = 0;
    leaseAt_ = quietAt_ = now;
  }
  void respond(const Frame& input, Type type, Code code) {
    Frame result = make(type);
    result.code = code; result.tap = input.tap; result.request = input.seq;
    cache(input, result);
  }
  void cache(const Frame& input, const Frame& output) {
    lastRx_ = input.seq; lastInput_ = input; lastOutput_ = output; cached_ = true;
    if (output.seq) output_.send(output);
  }
  Reader& reader_;
  Output& output_;
  Nonce outdoor_{}, indoor_{};
  bool bound_ = false, active_ = false, rearming_ = false, quiet_ = false, cached_ = false;
  uint32_t tx_ = 0, lastRx_ = 0, tapCounter_ = 0, tap_ = 0, expectedRequest_ = 1;
  uint32_t leaseAt_ = 0, tapAt_ = 0, commandAt_ = 0, tagSentAt_ = 0, quietAt_ = 0;
  Retry retry_ = Retry::None;
  uint32_t retryAt_ = 0;
  bool retried_ = false;
  Frame tag_{}, lastInput_{}, lastOutput_{};
};
}  // namespace outdoor
