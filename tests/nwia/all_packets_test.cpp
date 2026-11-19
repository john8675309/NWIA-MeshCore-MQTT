#include "../../nwia/BridgeCore.h"
#include <Packet.h>
#include <cassert>
#include <iostream>
#include <random>
#include <vector>

int main() {
  const size_t payloadSizes[16] = {20,20,20,4,100,19,19,51,20,10,5,1,0,0,0,1};
  const uint8_t origin[6]={1,2,3,4,5,6};
  for (int type=0;type<16;++type) {
    if (!payloadSizes[type]) continue;
    for (int route=0;route<4;++route) {
      if (type==9 && route<2) continue;
      for (int hashSize=1;hashSize<=3;++hashSize) {
        mesh::Packet packet;
        packet.header=(type<<2)|route;
        packet.setPathHashSizeAndCount(hashSize,2);
        for (int i=0;i<packet.getPathByteLen();++i) packet.path[i]=i+11;
        packet.payload_len=payloadSizes[type];
        memset(packet.payload,0,packet.payload_len);
        if(type==10)packet.payload[0]=3;
        packet.transport_codes[0]=0x1234;packet.transport_codes[1]=0xabcd;
        uint8_t raw[255],wire[nwia::kMaxWire];const auto length=packet.writeTo(raw);
        assert(nwia::validRaw(raw,length,nwia::Mode::All));
        auto n=nwia::encode(wire,origin,1800000000,raw,length,nwia::Mode::All);
        nwia::Envelope e{};assert(nwia::decode(wire,n,1800000001,e,nwia::Mode::All));
        assert(!nwia::decode(wire,n,1800000001,e,nwia::Mode::Channel));
        assert(nwia::decode(wire,n,1799999998,e,nwia::Mode::All));
        assert(!nwia::decode(wire,n,1799999997,e,nwia::Mode::All));
        mesh::Packet restored;assert(restored.readFrom(e.raw,e.rawSize));
        uint8_t copy[255];assert(restored.writeTo(copy)==length && !memcmp(raw,copy,length));
        for(size_t size=0;size<n;++size)assert(!nwia::decode(wire,size,1800000001,e,nwia::Mode::All));
      }
    }
  }
  // Maximum transport-coded frame: 6 + 64 path bytes + 184 payload bytes.
  std::vector<uint8_t> maximum(254,0);maximum[0]=(15<<2)|3;maximum[5]=0x60;
  assert(nwia::validRaw(maximum.data(),maximum.size(),nwia::Mode::All));
  maximum.push_back(0);assert(!nwia::validRaw(maximum.data(),maximum.size(),nwia::Mode::All));
  for(size_t n=0;n<6;++n)assert(!nwia::validRaw(maximum.data(),n,nwia::Mode::All));
  std::mt19937 rng(731);std::vector<uint8_t> fuzz(600);nwia::Envelope e{};
  for(int i=0;i<100000;++i) {
    const size_t n=rng()%fuzz.size();for(size_t j=0;j<n;++j)fuzz[j]=rng();
    if(nwia::validRaw(fuzz.data(),n,nwia::Mode::All)) { mesh::Packet p;assert(p.readFrom(fuzz.data(),n)); }
    nwia::decode(fuzz.data(),n,1800000000,e,nwia::Mode::All);
  }
  std::cout<<"PASS: all 13 MeshCore payload families, 4 route forms, path sizes, full-size frames, v1/v2 isolation and malformed frames\n";
}
