// Native bench utility: make a read-only MeshCore anonymous clock request.
// JSON on stdout includes an ephemeral ECDH secret for the bench caller only;
// callers must capture it in memory, never publish it or log it.
#include <Packet.h>
#include <Utils.h>
#include <ed_25519.h>
#include <fstream>
#include <iostream>
#include <ctime>
int main(int argc,char**argv){
 if(argc!=2)return 2;uint8_t dest[32],seed[32],pub[32],privateKey[64],shared[32];
 if(strlen(argv[1])!=64||!mesh::Utils::fromHex(dest,32,argv[1]))return 2;
 std::ifstream random("/dev/urandom",std::ios::binary);random.read((char*)seed,32);if(random.gcount()!=32)return 3;
 ed25519_create_keypair(pub,privateKey,seed);ed25519_key_exchange(shared,dest,privateKey);
 mesh::Packet p;p.header=(PAYLOAD_TYPE_ANON_REQ<<2)|ROUTE_TYPE_DIRECT;p.path_len=0;
 p.payload[0]=dest[0];memcpy(p.payload+1,pub,32);uint8_t body[6]{};
 const uint32_t now=time(nullptr);memcpy(body,&now,4);body[4]=3;body[5]=0; // BASIC request; zero-hop supplied reply path
 p.payload_len=33+mesh::Utils::encryptThenMAC(shared,p.payload+33,body,sizeof(body));
 uint8_t raw[255];const auto n=p.writeTo(raw);char hex[511],secret[65],client[65];
 mesh::Utils::toHex(hex,raw,n);mesh::Utils::toHex(secret,shared,32);mesh::Utils::toHex(client,pub,32);
 std::cout<<"{\"raw\":\""<<hex<<"\",\"secret\":\""<<secret<<"\",\"client\":\""<<client<<"\",\"timestamp\":"<<now<<"}\n";
 memset(seed,0,sizeof(seed));memset(privateKey,0,sizeof(privateKey));memset(shared,0,sizeof(shared));
}
