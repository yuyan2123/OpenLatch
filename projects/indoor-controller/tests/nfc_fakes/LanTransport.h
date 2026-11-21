#pragma once
#include <Protocol.h>
#include <deque>
#include <functional>
#include <utility>

namespace outdoor {
struct LanPeer {
  uint32_t address = 0;
  bool operator==(LanPeer other) const { return address == other.address; }
};
inline std::deque<std::pair<Wire, LanPeer>> fakeLanIncoming;
inline std::function<void(const Wire&, LanPeer)> fakeLanSend;
class LanTransport {
 public:
  LanTransport(const std::array<uint8_t, 32>&, bool) {}
  bool begin() { return true; }
  void stop() { fakeLanIncoming.clear(); }
  bool receive(Wire& wire, LanPeer& peer) {
    if (fakeLanIncoming.empty()) return false;
    wire = fakeLanIncoming.front().first;
    peer = fakeLanIncoming.front().second;
    fakeLanIncoming.pop_front();
    return true;
  }
  bool send(const Wire& wire, LanPeer peer) {
    if (fakeLanSend) fakeLanSend(wire, peer);
    return bool(fakeLanSend);
  }
};
}
