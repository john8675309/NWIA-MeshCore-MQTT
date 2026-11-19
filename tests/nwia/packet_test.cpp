#include <Packet.h>
#include <Utils.h>
#include <SHA256.h>
#include "../../nwia/BridgeCore.h"
#include <cassert>
#include <cstring>
#include <iostream>
int main() {
  uint8_t key[32]{}; for (int i=0;i<16;++i) key[i]=i+1;
  mesh::Packet p; p.header=(PAYLOAD_TYPE_GRP_TXT << 2) | ROUTE_TYPE_FLOOD;
  p.path_len=0; const uint8_t plain[]={0x00,0xd2,0x49,0x6b,0,'T','e','s','t',':',' ','h','i',0};
  mesh::Utils::sha256(p.payload,1,key,16);
  p.payload_len=1+mesh::Utils::encryptThenMAC(key,p.payload+1,plain,sizeof(plain));
  uint8_t raw[250]{};const int len=p.writeTo(raw);
  assert(nwia::validRaw(raw,len));
  mesh::Packet restored; assert(restored.readFrom(raw,len));
  uint8_t recovered[184]{};
  assert(mesh::Utils::MACThenDecrypt(key,recovered,restored.payload+1,restored.payload_len-1)>0);
  assert(!memcmp(recovered,plain,sizeof(plain)));
  uint8_t id1[16],id2[16];
  auto fingerprint=[](const mesh::Packet& packet,uint8_t* hash){SHA256 s;uint8_t t=packet.getPayloadType();s.update(&t,1);s.update(packet.payload,packet.payload_len);s.finalize(hash,16);};
  fingerprint(p,id1);
  restored.path_len=2;restored.path[0]=0x11;restored.path[1]=0x22;
  fingerprint(restored,id2);assert(!memcmp(id1,id2,16));
  uint8_t wire[nwia::kMaxWire],origin[6]={1,2,3,4,5,6};
  const int n=restored.writeTo(raw);
  auto wireLen=nwia::encode(wire,origin,1800000000,raw,n);
  nwia::Envelope e{};assert(nwia::decode(wire,wireLen,1800000001,e));
  mesh::Packet dest;assert(dest.readFrom(e.raw,e.rawSize));
  assert(dest.path_len==2 && dest.path[0]==0x11 && dest.path[1]==0x22);
  assert(dest.payload_len==p.payload_len && !memcmp(dest.payload,p.payload,p.payload_len));
  key[0]^=1;
  assert(mesh::Utils::MACThenDecrypt(key,recovered,dest.payload+1,dest.payload_len-1)==0);
  std::cout<<"PASS: actual MeshCore packet round trip, unchanged ciphertext/path, stable fingerprint, wrong-key rejection\n";
}
