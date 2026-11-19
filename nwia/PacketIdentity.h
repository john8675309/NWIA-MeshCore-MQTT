#pragma once
#include "BridgeCore.h"
#include <Packet.h>
#include <SHA256.h>
namespace nwia {
inline void packetFingerprint(const mesh::Packet& p, Mode mode, uint8_t hash[16]) {
  SHA256 sha;
  const uint8_t type=p.getPayloadType();sha.update(&type,1);
  if (mode == Mode::All && p.isRouteDirect()) {
    // A routed direct packet at the next native path step is new work, not an
    // echo of the foreign-hop copy previously mirrored at this gateway.
    const uint8_t route=p.getRouteType(),path=uint8_t(p.path_len),version=p.getPayloadVer();
    sha.update(&route,1);sha.update(&version,1);sha.update(&path,1);
    sha.update(p.path,p.getPathByteLen());
    if(p.hasTransportCodes())sha.update(p.transport_codes,sizeof(p.transport_codes));
  } else if(type == PAYLOAD_TYPE_TRACE)sha.update(&p.path_len,sizeof(p.path_len));
  sha.update(p.payload,p.payload_len);sha.finalize(hash,16);
}
inline bool mayExport(const mesh::Packet& p,Mode mode) {
  return !p._nwia_mqtt_ingress || (mode == Mode::All && p._nwia_mqtt_forward);
}
}
