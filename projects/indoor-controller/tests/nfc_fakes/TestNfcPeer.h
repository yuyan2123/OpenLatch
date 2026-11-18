#pragma once
#include <array>
#include <cstdint>
namespace credentials {
inline constexpr bool provisioned=true;
inline constexpr std::array<uint8_t,6> indoorMac{2,0,0,0,0,1},outdoorMac{2,0,0,0,0,2};
inline constexpr std::array<uint8_t,16> pmk{1},lmk{2};
inline constexpr std::array<uint8_t,32> hmacKey{3};
inline constexpr uint16_t channels=0x7ff;
}
