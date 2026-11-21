#pragma once
#include <cstdint>

namespace outdoor {
class ChannelScan {
 public:
  explicit ChannelScan(uint16_t allowed) : allowed_(allowed) {}
  uint8_t current() const { return current_; }
  void remember(uint8_t channel) {
    if (channel >= 1 && channel <= 13 && (allowed_ & (1u << (channel - 1)))) current_ = channel;
  }

  // Called after authenticated session processing, including lease expiry.
  bool probeDue(uint32_t now, bool bound) {
    if (bound) {
      retryCurrent_ = current_ != 0;
      return false;
    }
    if (!retryCurrent_ && current_ && uint32_t(now - lastProbe_) < 500) return false;
    if (!retryCurrent_) {
      // setupRadio validates that the mask contains at least one allowed channel.
      do {
        current_ = uint8_t(current_ % 13 + 1);
      } while (!(allowed_ & (1u << (current_ - 1))));
    }
    retryCurrent_ = false;
    lastProbe_ = now;
    return true;
  }

 private:
  const uint16_t allowed_;
  uint8_t current_ = 0;
  uint32_t lastProbe_ = 0;
  bool retryCurrent_ = false;
};
}  // namespace outdoor
