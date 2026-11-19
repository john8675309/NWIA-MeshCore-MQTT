#pragma once
#include <Mesh.h>

namespace nwia {
inline bool nextHopIsSelf(const mesh::Packet& p, const mesh::Identity& self) {
  if (!p.isRouteDirect()) return false;
  if (p.getPayloadType() == PAYLOAD_TYPE_TRACE) {
    if (p.payload_len < 9) return false;
    const size_t size = size_t(1) << (p.payload[8] & 3);
    const size_t offset = 9 + size_t(p.path_len) * size;
    return offset + size <= p.payload_len && self.isHashMatch(p.payload + offset, size);
  }
  return p.getPathHashCount() && self.isHashMatch(p.path, p.getPathHashSize());
}

// Use native routing whenever this gateway is a real hop. A direct packet for
// another hop is re-radiated unchanged at the remote site, like a virtual RF
// segment. Never consume someone else's path entry or bypass flood scope/hops.
// Callbacks let the native integration tests exercise the identical policy.
template<class Native, class Export>
mesh::DispatcherAction routePacket(mesh::Packet* packet, bool fullMode,
    bool forwardAllowed, bool nextHopSelf, bool alreadySeen,
    Native native, Export exportPacket) {
  if (!fullMode) return native();
  const mesh::Packet original = *packet;
  // Only actual consumption of our next-hop entry may make another network
  // leg. Foreign-hop mirrors keep this false. Set it before native ACK cloning.
  packet->_nwia_mqtt_forward = original._nwia_mqtt_ingress && nextHopSelf;
  const auto action = native();
  if (!forwardAllowed) return action;
  if (original._nwia_mqtt_ingress) {
    if (original.isRouteDirect() && !nextHopSelf && !alreadySeen &&
        !packet->isMarkedDoNotRetransmit() && action == ACTION_RELEASE) {
      *packet = original;
      // Do not mark the native payload-only seen table for a foreign mirror:
      // this gateway may legitimately become a later hop of the same packet.
      // Transport-stage fingerprints already suppress repeated mirror copies.
      return ACTION_RETRANSMIT(0);
    }
  } else if (action == ACTION_RELEASE &&
             (!original.isRouteDirect() || !nextHopSelf || packet->isMarkedDoNotRetransmit())) {
    exportPacket(original);
  }
  return action;
}
}
