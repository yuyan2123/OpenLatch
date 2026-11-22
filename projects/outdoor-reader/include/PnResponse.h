#pragma once
#include <cstddef>
#include <cstdint>

namespace outdoor {
// Frontend response contains InDataExchange opcode, PN532 status, then R-APDU.
// Nonzero/error/chained PN532 status must never be exposed as a complete R-APDU.
inline bool validPnResponse(const uint8_t* data, size_t length, size_t capacity) {
  return data && length >= 4 && data[0] == 0x41 && data[1] == 0 && length - 2 <= capacity;
}
inline bool retryablePnRfError(const uint8_t* data, size_t length) {
  // PN532 UM0701-02 §7.1. Only complete, unflagged status replies qualify.
  return data && length == 2 && data[0] == 0x41 && (data[1] == 0x01 || data[1] == 0x0b);
}
inline bool completedEcpBroadcast(const uint8_t* data, size_t length) {
  // An ECP broadcast normally receives no reply, reported as PN532 status 0x01.
  return data && length >= 2 && data[0] == 0x43 && (data[1] == 0 || (data[1] == 0x01 && length == 2));
}
}
