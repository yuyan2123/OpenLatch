#pragma once
#include <array>
#include <cstdint>
namespace credentials {
inline constexpr bool provisioned = false;
inline constexpr std::array<uint8_t, 6> indoorMac{}, outdoorMac{};
inline constexpr std::array<uint8_t, 16> pmk{}, lmk{};
inline constexpr std::array<uint8_t, 32> hmacKey{};
inline constexpr uint16_t channels = 0x07ff;
}
