#pragma once
#include <cstddef>
#include <cstdint>

namespace door {
inline constexpr char kFirmwareVersion[] = "1.5.0";
// Installed controllers match these exact bytes when accepting an OTA image.
inline constexpr char kFirmwarePrefix[] = "OpenLatch/DoorRelease/ESP32-C3/OTA1:";
inline constexpr uint8_t kFirmwareSuffix[]{1, 0xa7, 0x4c, 0x42, 0xb9, 0x67};
#ifdef DOOR_TEST_NFC_BUILD
inline constexpr char kFirmwareTag[] = "OpenLatch/DoorRelease/ESP32-C3/OTA1:T\x01\xa7\x4c\x42\xb9\x67";
#else
inline constexpr char kFirmwareTag[] = "OpenLatch/DoorRelease/ESP32-C3/OTA1:P\x01\xa7\x4c\x42\xb9\x67";
#endif
inline bool firmwareTestBuild() {
  // A volatile read retains the complete compatibility tag in the app image.
  const volatile char* tag = kFirmwareTag;
  return tag[sizeof(kFirmwarePrefix) - 1] == 'T';
}
}  // namespace door
