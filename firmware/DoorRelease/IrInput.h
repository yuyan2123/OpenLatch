#pragma once
#include <IRremote.hpp>
#include "Infrared.h"

inline door::IrKey irKey(const IRData& data) {
  door::IrProtocol protocol = door::IrProtocol::None;
  // Generic pulse decoders and protocols requiring extra identity fields are
  // excluded. NEC2/SamsungLG describe repeat styles of the same button.
  switch (data.protocol) {
    case NEC: case NEC2: protocol = door::IrProtocol::Nec; break;
    case SONY: protocol = door::IrProtocol::Sony; break;
    case SAMSUNG: case SAMSUNGLG: protocol = door::IrProtocol::Samsung; break;
    case RC5: protocol = door::IrProtocol::Rc5; break;
    case RC6: protocol = door::IrProtocol::Rc6; break;
    case JVC: protocol = door::IrProtocol::Jvc; break;
    default: break;
  }
  return {protocol, data.address, data.command, data.numberOfBits};
}
inline bool irRepeat(const IRData& data) {
  return data.flags & (IRDATA_FLAGS_IS_REPEAT | IRDATA_FLAGS_IS_AUTO_REPEAT);
}
inline bool irInvalid(const IRData& data) {
  return data.flags & (IRDATA_FLAGS_WAS_OVERFLOW | IRDATA_FLAGS_PARITY_FAILED | IRDATA_FLAGS_EXTRA_INFO);
}
