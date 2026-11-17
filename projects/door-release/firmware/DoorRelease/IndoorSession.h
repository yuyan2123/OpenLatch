#pragma once
#include <Protocol.h>
#include <limits>

namespace door {
// Codec/MAC checks happen before receive(). One NFC worker owns this session.
class IndoorSession {
 public:
  using Frame = outdoor::Frame;
  using Type = outdoor::Type;
  using Code = outdoor::Code;
  static constexpr uint32_t kRetryMs = 800, kRequestMs = 1600, kLeaseMs = 5000, kTapMs = 5000;
  void reset() { bound_ = waiting_ = tagReady_ = active_ = responseReady_ = false; }
  bool configure(uint32_t now, const Frame& hello, const outdoor::Nonce& nonce,
                 const std::array<uint8_t, 8>& group) {
    if (bound_ || waiting_ || hello.type != Type::Hello || hello.code != Code::Ok ||
        hello.length || hello.tap || hello.request || !hello.seq ||
        !outdoor::nonzero(hello.outdoor) || outdoor::nonzero(hello.indoor) || !outdoor::nonzero(nonce)) return false;
    reset(); outdoor_ = hello.outdoor; indoor_ = nonce; tx_ = 0; lastTap_ = 0;
    Frame f = make(Type::Configure); f.length = 8; std::copy(group.begin(), group.end(), f.payload.begin());
    request(now, f); return true;
  }
  void tick(uint32_t now) {
    if (waiting_ && uint32_t(now - requestAt_) >= kRequestMs) reset();
    else if (bound_ && uint32_t(now - leaseAt_) >= kLeaseMs) reset();
    else if (active_ && uint32_t(now - tapAt_) >= kTapMs) { active_ = tagReady_ = false; }
  }
  bool transmit(uint32_t now, Frame& output) {
    tick(now);
    if (!waiting_ || (sent_ && uint32_t(now - sentAt_) < kRetryMs)) return false;
    output = pending_; sent_ = true; sentAt_ = now; return true;
  }
  void receive(uint32_t now, const Frame& frame) {
    tick(now);
    if (frame.outdoor != outdoor_ || frame.indoor != indoor_ || !frame.seq || frame.length > outdoor::kMaxPayload) return;
    if (bound_ && frame.type == Type::Tag && frame.code == Code::Ok && frame.tap > lastTap_ &&
        !frame.request && frame.length >= 5 && frame.payload[0] >= 1 && frame.payload[0] <= 10 &&
        frame.length == uint16_t(frame.payload[0] + 4)) {
      if (active_) return;
      lastTap_ = tap_ = frame.tap; tapAt_ = now; requestId_ = 0;
      active_ = tagReady_ = true; return;
    }
    if (!waiting_) return;
    const bool exchange = pending_.type == Type::Exchange;
    if (frame.type != (exchange ? Type::Response : Type::Ack) ||
        frame.tap != pending_.tap || frame.request != (exchange ? pending_.request : pending_.seq)) return;
    if (frame.code != Code::Ok || (!exchange && frame.length) || (exchange && frame.length < 2)) { reset(); return; }
    waiting_ = false; leaseAt_ = now;
    if (pending_.type == Type::Configure) bound_ = true;
    if (exchange) { response_ = frame; responseReady_ = true; }
  }
  bool takeTag(uint32_t now) { tick(now); const bool ready = tagReady_; tagReady_ = false; return ready; }
  bool exchange(uint32_t now, const uint8_t* data, size_t length) {
    tick(now);
    if (!bound_ || !active_ || waiting_ || !data || length < 4 || length > outdoor::kMaxCommand ||
        tx_ >= std::numeric_limits<uint32_t>::max() - 2) return false;
    Frame f = make(Type::Exchange); f.tap = tap_; f.request = ++requestId_;
    f.length = uint16_t(length); std::copy_n(data, length, f.payload.begin());
    request(now, f); return true;
  }
  bool takeResponse(Frame& frame) {
    if (!responseReady_) return false;
    responseReady_ = false; frame = response_; return true;
  }
  bool release(uint32_t now) {
    tick(now); tagReady_ = false;
    if (!bound_ || !active_ || waiting_ || tx_ >= std::numeric_limits<uint32_t>::max() - 2) return false;
    Frame f = make(Type::Release); f.tap = tap_;
    active_ = false; request(now, f); return true;
  }
  bool ping(uint32_t now) {
    tick(now);
    if (!bound_ || waiting_ || active_ || tx_ >= std::numeric_limits<uint32_t>::max() - 2) return false;
    request(now, make(Type::Ping)); return true;
  }
  bool bound() const { return bound_; }
  bool waiting() const { return waiting_; }
  bool active() const { return active_; }
  const outdoor::Nonce& nonce() const { return indoor_; }
 private:
  Frame make(Type type) {
    Frame f; f.type = type; f.outdoor = outdoor_; f.indoor = indoor_; f.seq = ++tx_; return f;
  }
  void request(uint32_t now, const Frame& frame) {
    pending_ = frame; waiting_ = true; responseReady_ = false; sent_ = false; requestAt_ = now;
  }
  outdoor::Nonce outdoor_{}, indoor_{};
  bool bound_ = false, waiting_ = false, active_ = false, tagReady_ = false;
  bool sent_ = false, responseReady_ = false;
  uint32_t tx_ = 0, lastTap_ = 0, tap_ = 0, requestId_ = 0;
  uint32_t requestAt_ = 0, sentAt_ = 0, leaseAt_ = 0, tapAt_ = 0;
  Frame pending_{}, response_{};
};
}  // namespace door
