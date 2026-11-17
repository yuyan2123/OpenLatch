#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>

namespace door {
// Fixed storage and absolute deadlines apply before Arduino parses any body.
class HttpRequestGuard {
 public:
  static constexpr size_t kHeaderLimit = 2048, kFormLimit = 1024, kOtaLimit = 0x1e0000;
  static constexpr uint32_t kHeaderMs = 5000, kFormMs = 5000, kIdleMs = 2000;
  struct Header { const char* name; const char* value; };
  void begin(uint32_t now) { *this = {}; startedAt_ = lastAt_ = now; }
  bool headerByte(uint32_t now, uint8_t byte) {
    if (!tick(now) || ready_) return false;
    if ((byte == '\n' && (!used_ || bytes_[used_ - 1] != '\r')) ||
        (used_ && bytes_[used_ - 1] == '\r' && byte != '\n')) return fail(400);
    if (!byte || (byte < 32 && byte != '\r' && byte != '\n' && byte != '\t') || byte == 127) return fail(400);
    if (used_ == kHeaderLimit) return fail(431);
    bytes_[used_++] = char(byte); lastAt_ = now;
    if (used_ >= 4 && !std::memcmp(bytes_.data() + used_ - 4, "\r\n\r\n", 4)) return parse(now);
    return true;
  }
  bool bodyBytes(uint32_t now, size_t count) {
    if (!tick(now) || !ready_ || count > length_ - received_) return fail(400);
    if (count) lastAt_ = now;
    received_ += count; return true;
  }
  bool tick(uint32_t now) {
    if (error_) return false;
    const uint32_t total = ready_ ? (raw_ ? 180000u : kFormMs) : kHeaderMs;
    const uint32_t idle = ready_ && raw_ ? 15000u : kIdleMs;
    if (uint32_t(now - startedAt_) >= total || uint32_t(now - lastAt_) >= idle) return fail(408);
    return true;
  }
  bool ready() const { return ready_; }
  bool complete() const { return ready_ && received_ == length_; }
  bool post() const { return post_; }
  bool raw() const { return raw_; }
  int error() const { return error_; }
  size_t length() const { return length_; }
  size_t remaining() const { return length_ - received_; }
  const char* uri() const { return uri_; }
  const char* query() const { return query_; }
  size_t headerCount() const { return count_; }
  const Header& header(size_t i) const { return headers_[i]; }
  const char* value(const char* name) const {
    for (size_t i = 0; i < count_; ++i) if (equal(headers_[i].name, name)) return headers_[i].value;
    return "";
  }
  static bool equal(const char* a, const char* b) {
    for (; *a && *b; ++a, ++b) if (lower(*a) != lower(*b)) return false;
    return *a == *b;
  }
 private:
  static char lower(char c) { return c >= 'A' && c <= 'Z' ? char(c + 32) : c; }
  bool fail(int code) { if (!error_) error_ = code; return false; }
  bool parse(uint32_t now) {
    bytes_[used_] = 0;
    char* line = bytes_.data(); char* end = std::strstr(line, "\r\n");
    if (!end) return fail(400);
    *end = 0; char* path = std::strchr(line, ' ');
    if (!path) return fail(400);
    *path++ = 0; char* version = std::strchr(path, ' ');
    if (!version || path[0] != '/') return fail(400);
    *version++ = 0;
    if (std::strcmp(version, "HTTP/1.1") && std::strcmp(version, "HTTP/1.0")) return fail(400);
    if (std::strcmp(line, "GET") && std::strcmp(line, "POST")) return fail(405);
    post_ = !std::strcmp(line, "POST");
    if (std::strlen(path) > 256) return fail(414);
    for (const char* p = path; *p; ++p) if (uint8_t(*p) <= 32 || *p == '#') return fail(400);
    uri_ = path; char* query = std::strchr(path, '?');
    if (query) { *query++ = 0; query_ = query; }
    for (line = end + 2; *line && std::memcmp(line, "\r\n", 2); line = end + 2) {
      end = std::strstr(line, "\r\n");
      if (!end || end - line > 512 || count_ == headers_.size()) return fail(431);
      *end = 0; char* colon = std::strchr(line, ':');
      if (!colon || colon == line) return fail(400);
      *colon++ = 0;
      for (const char* p = line; *p; ++p) {
        const char c = lower(*p);
        if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-')) return fail(400);
      }
      while (*colon == ' ' || *colon == '\t') ++colon;
      char* tail = colon + std::strlen(colon);
      while (tail > colon && (tail[-1] == ' ' || tail[-1] == '\t')) *--tail = 0;
      // Reject ambiguous framing/authentication instead of picking one value.
      for (size_t i = 0; i < count_; ++i) if (equal(line, headers_[i].name)) return fail(400);
      headers_[count_++] = {line, colon};
    }
    if (!*value("Host") || *value("Transfer-Encoding")) return fail(400);
    if (*value("Expect")) return fail(417);
    const char* size = value("Content-Length");
    if (post_ && !*size) return fail(411);
    for (const char* p = size; *p; ++p) {
      if (*p < '0' || *p > '9') return fail(400);
      if (length_ > kOtaLimit / 10) return fail(413);
      length_ = length_ * 10 + size_t(*p - '0');
      if (length_ > kOtaLimit) return fail(413);
    }
    raw_ = post_ && !std::strcmp(uri_, "/api/firmware") && equal(value("Content-Type"), "application/octet-stream");
    if ((!post_ && length_) || (!raw_ && length_ > kFormLimit)) return fail(!post_ ? 400 : 413);
    if (post_ && !raw_) {
      const char* type = value("Content-Type");
      constexpr char form[] = "application/x-www-form-urlencoded";
      if ((length_ && std::strncmp(type, form, sizeof(form) - 1)) ||
          (*type && std::strlen(type) > sizeof(form) - 1 && type[sizeof(form) - 1] != ';')) return fail(415);
    }
    ready_ = true; startedAt_ = lastAt_ = now; return true;
  }
  std::array<char, kHeaderLimit + 1> bytes_{};
  std::array<Header, 24> headers_{};
  const char* uri_ = "", *query_ = "";
  size_t used_ = 0, count_ = 0, length_ = 0, received_ = 0;
  uint32_t startedAt_ = 0, lastAt_ = 0;
  int error_ = 0;
  bool ready_ = false, post_ = false, raw_ = false;
};
}  // namespace door
