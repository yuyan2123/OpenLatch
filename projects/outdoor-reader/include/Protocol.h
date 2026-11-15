#pragma once
#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>

namespace outdoor {
using Nonce = std::array<uint8_t, 16>;
using Mac = std::array<uint8_t, 6>;
constexpr size_t kHeaderSize = 56, kTagSize = 16, kMaxPayload = 1024;
constexpr size_t kMaxWireSize = kHeaderSize + kMaxPayload + kTagSize;
// Upstream PN532 emits a normal frame: APDU + command/Tg + TFI <= 255.
constexpr size_t kMaxCommand = 252;
enum class Type : uint8_t { Hello = 1, Configure, Ping, Tag, Exchange, Response, Release, Ack, Error };
enum class Code : uint8_t { Ok = 0, Invalid, NoTag, ReaderError, Expired };

struct Frame {
  Type type = Type::Hello;
  Code code = Code::Ok;
  uint32_t seq = 0, tap = 0, request = 0;
  Nonce outdoor{}, indoor{};
  uint16_t length = 0;
  std::array<uint8_t, kMaxPayload> payload{};
};
struct Wire {
  size_t length = 0;
  std::array<uint8_t, kMaxWireSize> bytes{};
};
struct Auth {
  virtual ~Auth() = default;
  virtual bool sign(const uint8_t* data, size_t length, uint8_t tag[kTagSize]) = 0;
};
inline bool nonzero(const Nonce& n) {
  uint8_t value = 0;
  for (auto b : n) value |= b;
  return value != 0;
}
inline bool fromIndoor(Type t) {
  return t == Type::Configure || t == Type::Ping || t == Type::Exchange || t == Type::Release;
}
inline void put32(uint8_t* p, uint32_t v) {
  for (int i = 0; i < 4; ++i) p[i] = uint8_t(v >> (8 * i));
}
inline uint32_t get32(const uint8_t* p) {
  uint32_t v = 0;
  for (int i = 0; i < 4; ++i) v |= uint32_t(p[i]) << (8 * i);
  return v;
}
inline void put16(uint8_t* p, uint16_t v) { p[0] = uint8_t(v); p[1] = uint8_t(v >> 8); }
inline uint16_t get16(const uint8_t* p) { return uint16_t(p[0]) | uint16_t(uint16_t(p[1]) << 8); }

// Explicit serialization avoids padding, alignment, and endian dependencies.
// Authenticated context binds the protocol, sender MAC, and destination MAC.
class Codec {
 public:
  Codec(Auth& auth, Mac indoor, Mac outdoor) : auth_(auth), indoor_(indoor), outdoor_(outdoor) {}
  bool encode(const Frame& f, bool senderIndoor, Wire& wire) {
    if (!valid(f) || fromIndoor(f.type) != senderIndoor) return false;
    auto* p = wire.bytes.data();
    std::memcpy(p, "OLNF", 4);
    p[4] = 1; p[5] = uint8_t(f.type); p[6] = uint8_t(f.code); p[7] = 0;
    put32(p + 8, f.seq); put32(p + 12, f.tap); put32(p + 16, f.request);
    put16(p + 20, f.length); p[22] = p[23] = 0;
    std::copy(f.outdoor.begin(), f.outdoor.end(), p + 24);
    std::copy(f.indoor.begin(), f.indoor.end(), p + 40);
    std::copy_n(f.payload.data(), f.length, p + kHeaderSize);
    const size_t body = kHeaderSize + f.length;
    if (!tag(p, body, senderIndoor, p + body)) return false;
    wire.length = body + kTagSize;
    return true;
  }
  bool decode(const uint8_t* p, size_t length, bool senderIndoor, Frame& result) {
    if (!p || length < kHeaderSize + kTagSize || length > kMaxWireSize) return false;
    if (std::memcmp(p, "OLNF", 4) || p[4] != 1 || p[7] || p[22] || p[23]) return false;
    const uint16_t payloadLength = get16(p + 20);
    if (payloadLength > kMaxPayload || length != kHeaderSize + payloadLength + kTagSize) return false;
    std::array<uint8_t, kTagSize> expected{};
    const size_t body = length - kTagSize;
    if (!tag(p, body, senderIndoor, expected.data())) return false;
    uint8_t mismatch = 0;
    for (size_t i = 0; i < kTagSize; ++i) mismatch |= expected[i] ^ p[body + i];
    if (mismatch) return false;
    Frame f;
    f.type = Type(p[5]); f.code = Code(p[6]);
    f.seq = get32(p + 8); f.tap = get32(p + 12); f.request = get32(p + 16);
    f.length = payloadLength;
    std::copy_n(p + 24, 16, f.outdoor.begin());
    std::copy_n(p + 40, 16, f.indoor.begin());
    std::copy_n(p + kHeaderSize, payloadLength, f.payload.begin());
    if (!valid(f) || fromIndoor(f.type) != senderIndoor) return false;
    result = f;
    return true;
  }
 private:
  static bool valid(const Frame& f) {
    return uint8_t(f.type) >= 1 && uint8_t(f.type) <= uint8_t(Type::Error) &&
           uint8_t(f.code) <= uint8_t(Code::Expired) && f.seq && f.length <= kMaxPayload;
  }
  bool tag(const uint8_t* p, size_t length, bool senderIndoor, uint8_t* output) {
    constexpr char domain[] = "OpenLatch/NFC/1";
    std::array<uint8_t, sizeof(domain) - 1 + 12 + kHeaderSize + kMaxPayload> data{};
    size_t pos = sizeof(domain) - 1;
    std::memcpy(data.data(), domain, pos);
    const auto& src = senderIndoor ? indoor_ : outdoor_;
    const auto& dst = senderIndoor ? outdoor_ : indoor_;
    for (auto b : src) data[pos++] = b;
    for (auto b : dst) data[pos++] = b;
    std::memcpy(data.data() + pos, p, length);
    return auth_.sign(data.data(), pos + length, output);
  }
  Auth& auth_;
  Mac indoor_, outdoor_;
};
inline bool same(const Frame& a, const Frame& b) {
  return a.type == b.type && a.code == b.code && a.seq == b.seq && a.tap == b.tap &&
         a.request == b.request && a.outdoor == b.outdoor && a.indoor == b.indoor &&
         a.length == b.length && std::equal(a.payload.begin(), a.payload.begin() + a.length, b.payload.begin());
}
}  // namespace outdoor
