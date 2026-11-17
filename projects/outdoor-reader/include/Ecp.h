#pragma once
#include <array>
#include <cstddef>
#include <cstdint>

namespace outdoor {
// ISO/IEC 14443-A CRC-A for the ECP polling frame.
inline uint16_t crcA(const uint8_t* data, size_t size) {
  uint16_t crc = 0x6363;
  for (size_t i = 0; i < size; ++i) {
    uint8_t b = data[i] ^ uint8_t(crc);
    b ^= uint8_t(b << 4);
    crc = uint16_t((crc >> 8) ^ (uint16_t(b) << 8) ^ (uint16_t(b) << 3) ^ (b >> 4));
  }
  return crc;
}
inline std::array<uint8_t, 18> makeEcp(const std::array<uint8_t, 8>& group) {
  std::array<uint8_t, 18> ecp{0x6a, 0x02, 0xcb, 0x02, 0x06, 0x02, 0x11, 0x00};
  for (size_t i = 0; i < group.size(); ++i) ecp[8 + i] = group[i];
  const auto crc = crcA(ecp.data(), 16);
  ecp[16] = uint8_t(crc); ecp[17] = uint8_t(crc >> 8);
  return ecp;
}
}  // namespace outdoor
