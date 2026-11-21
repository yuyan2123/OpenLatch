#pragma once
#include "Protocol.h"
#include <esp_random.h>
#include <lwip/sockets.h>
#include <mbedtls/gcm.h>
#include <mbedtls/md.h>

namespace outdoor {
struct LanPeer {
  uint32_t address = 0; // IPv4, network byte order
  bool operator==(LanPeer other) const { return address == other.address; }
  bool operator!=(LanPeer other) const { return !(*this == other); }
};

// The owner runs all socket and crypto operations on its NFC task.
class LanTransport {
 public:
  LanTransport(const std::array<uint8_t, 32>& key, bool indoor) : key_(key), indoor_(indoor) {
    mbedtls_gcm_init(&tx_);
    mbedtls_gcm_init(&rx_);
  }
  ~LanTransport() { stop(); mbedtls_gcm_free(&tx_); mbedtls_gcm_free(&rx_); }
  LanTransport(const LanTransport&) = delete;
  LanTransport& operator=(const LanTransport&) = delete;

  bool begin(uint32_t address = 0) {
    if (fd_ >= 0 && address == local_) return true;
    stop();
    if (!keysReady_) {
      keysReady_ = derive(tx_, indoor_) && derive(rx_, !indoor_);
      if (!keysReady_) return false;
    }
    fd_ = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (fd_ < 0) return false;
    const int enable = 1;
    const auto local = endpoint(address);
    if (setsockopt(fd_, SOL_SOCKET, SO_BROADCAST, &enable, sizeof(enable)) != 0 ||
        bind(fd_, reinterpret_cast<const sockaddr*>(&local), sizeof(local)) != 0) {
      stop();
      return false;
    }
    local_ = address;
    return true;
  }

  void stop() {
    if (fd_ >= 0) { close(fd_); fd_ = -1; }
    local_ = 0;
  }

  bool send(const Wire& wire, LanPeer peer = {}) {
    if (fd_ < 0 || !validSize(wire.length)) return false;
    auto* packet = packet_.data();
    std::copy(kMagic.begin(), kMagic.end(), packet);
    auto* nonce = packet + kMagic.size();
    esp_fill_random(nonce, kNonceSize);
    auto* ciphertext = packet + kPrefixSize;
    if (mbedtls_gcm_crypt_and_tag(&tx_, MBEDTLS_GCM_ENCRYPT, wire.length,
                                 nonce, kNonceSize, packet, kMagic.size(),
                                 wire.bytes.data(), ciphertext, kTagSize, ciphertext + wire.length)) return false;
    const auto target = endpoint(peer.address ? peer.address : htonl(INADDR_BROADCAST));
    const size_t size = kPrefixSize + wire.length + kTagSize;
    return sendto(fd_, packet, size, MSG_DONTWAIT, reinterpret_cast<const sockaddr*>(&target), sizeof(target)) == int(size);
  }

  bool receive(Wire& wire, LanPeer& peer) {
    if (fd_ < 0) return false;
    sockaddr_in source{};
    socklen_t length = sizeof(source);
    const int received = recvfrom(fd_, packet_.data(), packet_.size(), MSG_DONTWAIT,
                                  reinterpret_cast<sockaddr*>(&source), &length);
    if (received < int(kPrefixSize + kTagSize) || source.sin_family != AF_INET ||
        source.sin_port != htons(kPort) || !source.sin_addr.s_addr ||
        !std::equal(kMagic.begin(), kMagic.end(), packet_.begin())) return false;
    const size_t body = size_t(received) - kPrefixSize - kTagSize;
    if (!validSize(body)) return false;
    const auto* ciphertext = packet_.data() + kPrefixSize;
    if (mbedtls_gcm_auth_decrypt(&rx_, body, packet_.data() + kMagic.size(), kNonceSize,
                                packet_.data(), kMagic.size(), ciphertext + body, kTagSize,
                                ciphertext, wire.bytes.data())) return false;
    wire.length = body;
    peer.address = source.sin_addr.s_addr;
    return true;
  }

 private:
  static constexpr uint16_t kPort = 31932;
  static constexpr std::array<uint8_t, 4> kMagic{'O', 'L', 'W', '1'};
  static constexpr size_t kNonceSize = 12, kPrefixSize = kMagic.size() + kNonceSize;
  static bool validSize(size_t length) { return length >= kHeaderSize + kTagSize && length <= kMaxWireSize; }
  static sockaddr_in endpoint(uint32_t address) {
    sockaddr_in result{};
    result.sin_family = AF_INET;
    result.sin_port = htons(kPort);
    result.sin_addr.s_addr = address;
    return result;
  }
  bool derive(mbedtls_gcm_context& context, bool senderIndoor) {
    unsigned char label[] = "OpenLatch/NFC/WiFi/1/0";
    label[sizeof(label) - 2] = senderIndoor ? '1' : '0';
    std::array<uint8_t, 32> derived{};
    const auto* md = mbedtls_md_info_from_type(MBEDTLS_MD_SHA256);
    return md && !mbedtls_md_hmac(md, key_.data(), key_.size(), label, sizeof(label) - 1, derived.data()) &&
           !mbedtls_gcm_setkey(&context, MBEDTLS_CIPHER_ID_AES, derived.data(), 256);
  }

  const std::array<uint8_t, 32>& key_;
  bool indoor_, keysReady_ = false;
  int fd_ = -1;
  uint32_t local_ = 0;
  mbedtls_gcm_context tx_, rx_;
  std::array<uint8_t, kPrefixSize + kMaxWireSize + kTagSize + 1> packet_{};
};
}  // namespace outdoor
