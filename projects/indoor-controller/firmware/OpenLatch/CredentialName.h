#pragma once
#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>

namespace door {
class CredentialName {
 public:
  static constexpr size_t kMaxBytes = 48;

  static bool parse(const char* text, size_t length, CredentialName& result) {
    while (length && *text == ' ') { ++text; --length; }
    while (length && text[length - 1] == ' ') --length;
    if (length > kMaxBytes) return false;
    const auto* bytes = reinterpret_cast<const uint8_t*>(text);
    for (size_t i = 0; i < length;) {
      const uint8_t start = bytes[i++];
      if (start < 0x80) {
        if (start < 0x20 || start == 0x7f) return false;
        continue;
      }
      if (start < 0xc2 || start > 0xf4) return false;
      const size_t extra = start < 0xe0 ? 1 : start < 0xf0 ? 2 : 3;
      if (length - i < extra) return false;
      const uint8_t second = bytes[i];
      if ((start == 0xc2 && second < 0xa0) || (start == 0xe0 && second < 0xa0) ||
          (start == 0xed && second >= 0xa0) || (start == 0xf0 && second < 0x90) ||
          (start == 0xf4 && second >= 0x90)) return false;
      for (size_t end = i + extra; i < end; ++i)
        if ((bytes[i] & 0xc0) != 0x80) return false;
    }
    result = {};
    std::copy_n(text, length, result.text_.begin());
    return true;
  }

  bool empty() const { return text_[0] == 0; }
  const char* c_str() const { return text_.data(); }

 private:
  std::array<char, kMaxBytes + 1> text_{};
};
}  // namespace door
