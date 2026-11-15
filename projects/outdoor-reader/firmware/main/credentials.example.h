#pragma once
#include <array>
#include <cstdint>

namespace credentials {
// Buildable but inactive. Generate private/credentials.h using tools/provision.py.
inline constexpr bool provisioned = false;
inline constexpr std::array<uint8_t, 6> indoorMac{};
inline constexpr std::array<uint8_t, 6> outdoorMac{};
inline constexpr std::array<uint8_t, 16> pmk{};
inline constexpr std::array<uint8_t, 16> lmk{};
inline constexpr std::array<uint8_t, 32> hmacKey{};
// Channels 1-11. Discovery never accepts an unauthenticated channel command.
inline constexpr uint16_t channels = 0x07ff;
}
