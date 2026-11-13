#pragma once
#include <cstddef>

// Bounded USB command input; reject an entire malformed/oversized line.
class SerialLine {
 public:
  enum class Result { Pending, Complete, Rejected };
  Result feed(char value) {
    if (value == '\r' || value == '\n') {
      if (bad_) { reset(); return Result::Rejected; }
      if (!length_) return Result::Pending;
      buffer_[length_] = '\0';
      length_ = 0;
      return Result::Complete;
    }
    if (value < 32 || value > 126 || length_ >= sizeof(buffer_) - 1) bad_ = true;
    if (!bad_) buffer_[length_++] = value;
    return Result::Pending;
  }
  const char* value() const { return buffer_; }

 private:
  void reset() { length_ = 0; bad_ = false; }
  char buffer_[48]{};
  size_t length_ = 0;
  bool bad_ = false;
};
