#pragma once
#include <cstdint>
#include <limits>
#include <mutex>

namespace door {
constexpr uint32_t kNfcMaxFailures = 5, kNfcLockoutSeconds = 60;
constexpr uint32_t kNfcAttemptTimeoutMs = 5000;
inline bool validNfcLimits(uint32_t failures, uint32_t seconds) {
  return failures >= 1 && failures <= 20 && seconds >= 1 && seconds <= 3600;
}

// Owned by the indoor verifier task. Transport packets and card UIDs are not
// authentication outcomes. One completed verification may consume one attempt.
class NfcAttemptLimiter {
 public:
  enum class Verdict { Authorized, InvalidCredential, Aborted };
  // Boot configuration only. Counters are volatile; settings are stored by NVS.
  void begin(uint32_t failures = kNfcMaxFailures, uint32_t seconds = kNfcLockoutSeconds) {
    std::lock_guard<std::mutex> lock(mutex_);
    ready_ = validNfcLimits(failures, seconds);
    limit_ = failures; lockMs_ = ready_ ? seconds * 1000 : 0;
    failures_ = 0; locked_ = false; pending_ = 0;
    // Do not reuse IDs if the owner reinitializes this object.
  }
  uint64_t start(uint32_t now) {
    std::lock_guard<std::mutex> lock(mutex_); tickLocked(now);
    if (!ready_ || locked_ || pending_ || next_ == std::numeric_limits<uint64_t>::max()) return 0;
    pending_ = ++next_; startedAt_ = now;
    return pending_;
  }
  // Only true permits the indoor verifier to proceed to the relay controller.
  // Duplicate, stale, timed-out and blocked completions cannot authorize.
  bool complete(uint32_t now, uint64_t attempt, Verdict verdict) {
    std::lock_guard<std::mutex> lock(mutex_); tickLocked(now);
    if (!ready_ || locked_ || !attempt || attempt != pending_) return false;
    pending_ = 0;
    if (verdict == Verdict::Authorized) { failures_ = 0; return true; }
    if (verdict == Verdict::InvalidCredential && ++failures_ >= limit_) {
      locked_ = true; lockedAt_ = now;
    }
    return false;
  }
  void tick(uint32_t now) { std::lock_guard<std::mutex> lock(mutex_); tickLocked(now); }
  bool ready() const { std::lock_guard<std::mutex> lock(mutex_); return ready_; }
  bool locked(uint32_t now) { std::lock_guard<std::mutex> lock(mutex_); tickLocked(now); return locked_; }
  uint32_t failures(uint32_t now) { std::lock_guard<std::mutex> lock(mutex_); tickLocked(now); return failures_; }
  uint32_t remainingMs(uint32_t now) {
    std::lock_guard<std::mutex> lock(mutex_); tickLocked(now);
    return locked_ ? lockMs_ - uint32_t(now - lockedAt_) : 0;
  }
 private:
  void tickLocked(uint32_t now) {
    if (pending_ && uint32_t(now - startedAt_) >= kNfcAttemptTimeoutMs) pending_ = 0;
    if (locked_ && uint32_t(now - lockedAt_) >= lockMs_) { locked_ = false; failures_ = 0; }
  }
  mutable std::mutex mutex_;
  bool ready_ = false, locked_ = false;
  uint32_t limit_ = 0, lockMs_ = 0, failures_ = 0, startedAt_ = 0, lockedAt_ = 0;
  uint64_t next_ = 0, pending_ = 0;
};
}  // namespace door
