#pragma once
#include <cstddef>
#include <cstdint>

namespace outdoor {
// Frontend response contains InDataExchange opcode, PN532 status, then R-APDU.
// Nonzero/error/chained PN532 status must never be exposed as a complete R-APDU.
inline bool validPnResponse(const uint8_t* data, size_t length, size_t capacity) {
  return data && length >= 4 && data[0] == 0x41 && data[1] == 0 && length - 2 <= capacity;
}
}
