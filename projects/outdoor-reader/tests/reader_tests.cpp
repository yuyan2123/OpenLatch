#include "Protocol.h"
#include "ChannelScan.h"
#include "ReaderSession.h"
#include "Ecp.h"
#include "PnResponse.h"
#include <cstdlib>
#include <iostream>
#include <random>
#include <vector>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <bcrypt.h>
#else
#include <openssl/hmac.h>
#endif

#define CHECK(x) do { if (!(x)) { std::cerr << __LINE__ << ": " << #x << '\n'; std::exit(1); } } while (false)
using namespace outdoor;

struct Hmac : Auth {
  std::vector<uint8_t> key = std::vector<uint8_t>(32, 0x5a);
  bool sign(const uint8_t* data, size_t size, uint8_t* tag) override {
    std::array<uint8_t, 32> digest{};
#ifdef _WIN32
    BCRYPT_ALG_HANDLE algorithm = nullptr;
    BCRYPT_HASH_HANDLE hash = nullptr;
    if (BCryptOpenAlgorithmProvider(&algorithm, BCRYPT_SHA256_ALGORITHM, nullptr, BCRYPT_ALG_HANDLE_HMAC_FLAG)) return false;
    NTSTATUS status = BCryptCreateHash(algorithm, &hash, nullptr, 0, key.data(), ULONG(key.size()), 0);
    if (!status) status = BCryptHashData(hash, const_cast<PUCHAR>(data), ULONG(size), 0);
    if (!status) status = BCryptFinishHash(hash, digest.data(), ULONG(digest.size()), 0);
    if (hash) BCryptDestroyHash(hash);
    BCryptCloseAlgorithmProvider(algorithm, 0);
    if (status) return false;
#else
    unsigned int length = 0;
    if (!HMAC(EVP_sha256(), key.data(), int(key.size()), data, size, digest.data(), &length) || length != 32) return false;
#endif
    std::copy_n(digest.begin(), kTagSize, tag);
    return true;
  }
};
struct FakeReader : Reader {
  int configurations = 0, exchanges = 0, releases = 0;
  bool available = true, failExchange = false;
  size_t responseLength = 2;
  std::vector<uint8_t> command;
  bool configure(const std::array<uint8_t, 8>&) override { ++configurations; return available; }
  bool exchange(const uint8_t* p, size_t size, uint8_t* output, size_t& capacity) override {
    ++exchanges; command.assign(p, p + size);
    if (failExchange) return false;
    CHECK(capacity >= 2); output[0] = 0x90; output[1] = 0;
    capacity = responseLength; return true;
  }
  void release() override { ++releases; }
};
struct Sink : Output {
  int epochs = 0;
  std::vector<Frame> sent;
  void send(const Frame& frame) override { CHECK(frame.seq); sent.push_back(frame); }
  Nonce randomNonce() override { Nonce nonce{}; nonce[0] = uint8_t(++epochs); return nonce; }
};
struct Fixture {
  FakeReader reader;
  Sink sink;
  ReaderSession session{reader, sink};
  uint32_t start;
  Frame config;
  explicit Fixture(uint32_t now = 0) : start(now) {
    session.begin(now);
    config.type = Type::Configure; config.seq = 1;
    config.outdoor = session.nonce(); config.indoor[0] = 0xa1;
    config.length = 8;
  }
  void bind() {
    session.receive(start, config); CHECK(session.bound());
    session.presence(start, false);
    session.presence(start + 500, false);
    CHECK(!session.rearming());
  }
  uint32_t tap(uint32_t offset = 500) {
    uint8_t uid[]{1, 2, 3, 4};
    CHECK(session.detected(start + offset, uid, sizeof(uid), {0x04, 0}, 0x20));
    return sink.sent.back().tap;
  }
  Frame command(uint32_t seq = 2, uint32_t request = 1) {
    Frame f = config;
    f.type = Type::Exchange; f.seq = seq; f.request = request; f.tap = sink.sent.back().tap;
    f.length = 4; f.payload[0] = 0; f.payload[1] = 0xa4; f.payload[2] = 4; f.payload[3] = 0;
    return f;
  }
};

void cryptoAndWire() {
  Hmac auth;
  auth.key.assign(20, 0x0b);
  uint8_t tag[kTagSize];
  CHECK(auth.sign(reinterpret_cast<const uint8_t*>("Hi There"), 8, tag));
  const uint8_t rfc4231[]{0xb0,0x34,0x4c,0x61,0xd8,0xdb,0x38,0x53,0x5c,0xa8,0xaf,0xce,0xaf,0x0b,0xf1,0x2b};
  CHECK(std::equal(tag, tag + kTagSize, rfc4231));
  Mac indoor{2,1,2,3,4,5}, outside{2,6,7,8,9,10};
  Codec codec(auth, indoor, outside);
  Frame input; input.type = Type::Exchange; input.seq = 42; input.tap = 9;
  input.request = 3; input.length = 255; input.outdoor[0] = 1; input.indoor[0] = 2;
  for (size_t i = 0; i < input.length; ++i) input.payload[i] = uint8_t(i);
  Wire wire;
  CHECK(codec.encode(input, true, wire));
  const uint8_t prefix[]{'O','L','N','F',1,5,0,0,42,0,0,0,9,0,0,0,3,0,0,0,255,0,0,0};
  CHECK(std::equal(std::begin(prefix), std::end(prefix), wire.bytes.begin()));
  // Independently generated with Python struct.pack + stdlib hmac/hashlib.
  const uint8_t expectedTag[]{0x51,0x77,0x77,0x85,0x84,0xc4,0xdf,0xd1,0xe9,0x06,0x9b,0x96,0xa9,0x2e,0xb2,0x08};
  CHECK(std::equal(std::begin(expectedTag), std::end(expectedTag), wire.bytes.begin() + wire.length - kTagSize));
  Frame decoded;
  CHECK(codec.decode(wire.bytes.data(), wire.length, true, decoded));
  CHECK(same(input, decoded));
  CHECK(!codec.decode(wire.bytes.data(), wire.length, false, decoded));
  Codec swapped(auth, outside, indoor);
  CHECK(!swapped.decode(wire.bytes.data(), wire.length, true, decoded));
  for (size_t length = 0; length < wire.length; ++length)
    CHECK(!codec.decode(wire.bytes.data(), length, true, decoded));
  CHECK(!codec.decode(wire.bytes.data(), wire.length + 1, true, decoded));
  CHECK(!codec.decode(nullptr, wire.length, true, decoded));
  for (size_t byte = 0; byte < wire.length; ++byte) {
    for (int bit = 0; bit < 8; ++bit) {
      wire.bytes[byte] ^= uint8_t(1 << bit);
      CHECK(!codec.decode(wire.bytes.data(), wire.length, true, decoded));
      wire.bytes[byte] ^= uint8_t(1 << bit);
    }
  }
  input.type = Type::Response; input.length = kMaxPayload;
  CHECK(codec.encode(input, false, wire));
  CHECK(wire.length == kMaxWireSize);
  CHECK(codec.decode(wire.bytes.data(), wire.length, false, decoded));
  input.length = kMaxPayload + 1; CHECK(!codec.encode(input, false, wire));
  input.length = 0; input.seq = 0; CHECK(!codec.encode(input, false, wire));
  std::mt19937 random(20261116);
  for (int i = 0; i < 5000; ++i) {
    size_t length = random() % wire.bytes.size();
    for (size_t j = 0; j < length; ++j) wire.bytes[j] = uint8_t(random());
    CHECK(!codec.decode(wire.bytes.data(), length, true, decoded));
  }
  std::cout << "PASS: RFC 4231 HMAC, authenticated codec, address/direction binding, every single-bit corruption, bounds and 5000 malformed packets.\n";
}

void pairingAndRetries() {
  Fixture f;
  auto wrong = f.config; wrong.outdoor[0] ^= 1;
  f.session.receive(0, wrong); CHECK(!f.session.bound());
  wrong = f.config; wrong.indoor = {};
  f.session.receive(0, wrong); CHECK(!f.session.bound());
  wrong = f.config; wrong.seq = 2;
  f.session.receive(0, wrong); CHECK(!f.session.bound());
  f.bind();
  Frame ack = f.sink.sent.back();
  f.session.receive(501, f.config);
  CHECK(f.reader.configurations == 1 && same(ack, f.sink.sent.back()));
  wrong = f.config; wrong.indoor[0] ^= 1;
  f.session.receive(501, wrong); CHECK(f.reader.configurations == 1);
  f.tap();
  Frame tag = f.sink.sent.back();
  f.session.receive(550, f.config);
  CHECK(f.reader.configurations == 1 && same(ack, f.sink.sent.back()));
  f.session.tick(650); CHECK(same(tag, f.sink.sent.back()));
  auto command = f.command();
  f.session.receive(700, command);
  CHECK(f.reader.exchanges == 1 && f.reader.command.size() == 4);
  Frame response = f.sink.sent.back();
  CHECK(response.type == Type::Response && response.payload[0] == 0x90);
  f.session.receive(800, command);
  CHECK(f.reader.exchanges == 1 && same(response, f.sink.sent.back()));
  wrong = command; wrong.payload[0] ^= 1;
  f.session.receive(900, wrong); CHECK(f.reader.exchanges == 1);
  wrong = command; wrong.seq = 3;
  f.session.receive(900, wrong); CHECK(f.reader.exchanges == 1);
  wrong = command; wrong.seq = 3; wrong.request = 2; ++wrong.tap;
  f.session.receive(900, wrong); CHECK(f.reader.exchanges == 1);
  wrong = command; wrong.seq = 3; wrong.request = 2; wrong.length = 253;
  f.session.receive(900, wrong); CHECK(f.reader.exchanges == 1);
  command.seq = 3; command.request = 2; command.length = kMaxCommand;
  f.session.receive(1000, command); CHECK(f.reader.exchanges == 2 && f.reader.command.size() == kMaxCommand);
  Frame release = command; release.type = Type::Release; release.seq = 4; release.request = 0; release.length = 0;
  f.session.receive(1100, release);
  CHECK(!f.session.active() && f.reader.releases == 1);
  f.session.receive(1200, release); CHECK(f.reader.releases == 1);
  f.session.receive(1200, command); CHECK(f.reader.exchanges == 2);
  std::cout << "PASS: nonce handshake, tag announcement retry, at-most-once APDU execution, exact response retry, stale/wrong tap and command rejection.\n";
}

void lifetime(uint32_t start) {
  Fixture f(start); f.bind(); f.tap();
  auto command = f.command();
  f.session.receive(start + 700, command);
  f.session.tick(start + 2199); CHECK(f.session.active());
  f.session.tick(start + 2200); CHECK(!f.session.active());
  int exchanges = f.reader.exchanges;
  f.session.receive(start + 2300, command); CHECK(f.reader.exchanges == exchanges);
  f.session.presence(start + 2300, false);
  f.session.presence(start + 2600, true);
  f.session.presence(start + 2700, false);
  f.session.presence(start + 3199, false); CHECK(f.session.rearming());
  f.session.presence(start + 3200, false); CHECK(!f.session.rearming());
  auto nonce = f.session.nonce();
  // Exact old retries cannot keep a lease alive.
  f.session.receive(start + 5699, command);
  f.session.tick(start + 5700);
  CHECK(!f.session.bound() && f.session.nonce() != nonce);
  f.session.receive(start + 5701, f.config); CHECK(!f.session.bound());
}
void faultsAndHardDeadline() {
  lifetime(0); lifetime(0xfffffc00u);
  Fixture broken; broken.reader.available = false;
  broken.session.receive(0, broken.config); CHECK(!broken.session.bound());
  CHECK(broken.session.nonce() != broken.config.outdoor);
  for (size_t badSize : {size_t(0), size_t(1), size_t(1025)}) {
    Fixture f; f.bind(); f.tap(); f.reader.responseLength = badSize;
    f.session.receive(600, f.command());
    CHECK(!f.session.active()); CHECK(f.sink.sent.back().code == Code::ReaderError);
  }
  Fixture f; f.bind(); f.tap();
  for (uint32_t n = 1; n <= 5; ++n) {
    auto command = f.command(n + 1, n);
    f.session.receive(500 + n * 900, command);
    CHECK(f.session.active());
  }
  f.session.tick(5499); CHECK(f.session.active());
  f.session.tick(5500); CHECK(!f.session.active());
  uint8_t message[]{'1','2','3','4','5','6','7','8','9'};
  CHECK(crcA(message, sizeof(message)) == 0xbf05);
  auto ecp = makeEcp({1,2,3,4,5,6,7,8});
  CHECK(ecp[0] == 0x6a && ecp[8] == 1 && ecp[15] == 8);
  CHECK(crcA(ecp.data(), ecp.size()) == 0);
  uint8_t raw[]{0x41, 0, 0x90, 0};
  CHECK(validPnResponse(raw, sizeof(raw), 2));
  CHECK(!validPnResponse(raw, sizeof(raw), 1));
  CHECK(!validPnResponse(raw, 3, 2));
  raw[1] = 1; CHECK(!validPnResponse(raw, sizeof(raw), 2));
  raw[1] = 0x40; CHECK(!validPnResponse(raw, sizeof(raw), 2));
  raw[1] = 0; raw[0] = 0x43; CHECK(!validPnResponse(raw, sizeof(raw), 2));
  std::cout << "PASS: lease/tap deadlines across clock wrap, removal gate, reader failures, old-epoch rejection and ECP CRC-A.\n";
}
void channelScanning() {
  constexpr uint32_t start = 0xffffff00u;
  ChannelScan scan((1u << 0) | (1u << 5) | (1u << 12));
  CHECK(scan.probeDue(start, false) && scan.current() == 1);
  CHECK(!scan.probeDue(start + 499, false));
  CHECK(scan.probeDue(start + 500, false) && scan.current() == 6);
  CHECK(!scan.probeDue(start + 501, true));
  CHECK(scan.probeDue(start + 502, false) && scan.current() == 6);
  CHECK(!scan.probeDue(start + 1001, false));
  CHECK(scan.probeDue(start + 1002, false) && scan.current() == 13);
  CHECK(scan.probeDue(start + 1502, false) && scan.current() == 1);
  scan.remember(6);
  CHECK(!scan.probeDue(start + 1503, true));
  CHECK(scan.probeDue(start + 1504, false) && scan.current() == 6);
  ChannelScan single(1u << 10);
  single.remember(13);
  CHECK(!single.probeDue(0, true));
  CHECK(single.probeDue(0, false) && single.current() == 11);
  CHECK(single.probeDue(500, false) && single.current() == 11);
  std::cout << "PASS: preferred channel on reconnect, masked fallback, single channel and clock wrap.\n";
}
int main() { cryptoAndWire(); pairingAndRetries(); faultsAndHardDeadline(); channelScanning(); }
