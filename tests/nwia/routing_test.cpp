#include <Mesh.h>
#include "../../nwia/BridgeCore.h"
#include "../../nwia/PacketIdentity.h"
#include <array>
#include <set>
#include <helpers/bridges/NwiaRouting.h>
#include <helpers/StaticPoolPacketManager.h>
#include <helpers/SimpleMeshTables.h>
#include <cassert>
#include <iostream>
#include <vector>
struct Clock:mesh::MillisecondClock { unsigned long now=1000;unsigned long getMillis() override{return now;} };
struct RTC:mesh::RTCClock {uint32_t getCurrentTime() override{return 1800000000;} void setCurrentTime(uint32_t)override{}};
struct RNG:mesh::RNG { uint8_t value=1;void random(uint8_t*d,size_t n)override{for(size_t i=0;i<n;++i)d[i]=value++;} };
struct Radio:mesh::Radio {int sends=0;int recvRaw(uint8_t*,int)override{return 0;}uint32_t getEstAirtimeFor(int)override{return 1;}float packetScore(float,int)override{return 1;}bool startSendRaw(const uint8_t*,int)override{sends++;return true;}bool isSendComplete()override{return true;}void onSendFinished()override{}bool isInRecvMode()const override{return true;}};
struct Gateway:mesh::Mesh {
  bool forwarding=true;int exports=0;mesh::Packet exported;std::set<std::array<uint8_t,16>> transportSeen;
  Gateway(Radio&r,Clock&c,RNG&g,RTC&t,StaticPoolPacketManager&m,SimpleMeshTables&s,uint8_t id):Mesh(r,c,g,t,m,s){memset(self_id.pub_key,id,32);}
  bool allowPacketForward(const mesh::Packet*)override{return forwarding;}
  uint32_t getRetransmitDelay(const mesh::Packet*)override{return 0;}
  uint32_t getDirectRetransmitDelay(const mesh::Packet*)override{return 0;}
  mesh::DispatcherAction receive(mesh::Packet& p) {
    if(p._nwia_mqtt_ingress){std::array<uint8_t,16> hash;nwia::packetFingerprint(p,nwia::Mode::All,hash.data());if(!transportSeen.insert(hash).second)return ACTION_RELEASE;}
    bool next=nwia::nextHopIsSelf(p,self_id);
    bool seen=p._nwia_mqtt_ingress&&p.isRouteDirect()&&getTables()->wasSeen(&p);
    return nwia::routePacket(&p,true,forwarding,next,seen,
      [&](){return Mesh::onRecvPacket(&p);},
      [&](const mesh::Packet&p){exports++;exported=p;});
  }
};
struct Endpoint:mesh::Mesh {
 mesh::Identity peer;uint8_t shared[32]{};std::vector<uint8_t> received;int adverts=0,anonymous=0,paths=0;
 Endpoint(Radio&r,Clock&c,RNG&g,RTC&t,StaticPoolPacketManager&m,SimpleMeshTables&s):Mesh(r,c,g,t,m,s){self_id=mesh::LocalIdentity(&g);}
 void pair(const mesh::Identity& id){peer=id;self_id.calcSharedSecret(shared,id);}
 int searchPeersByHash(const uint8_t*h)override{return peer.isHashMatch(h)?1:0;}
 void getPeerSharedSecret(uint8_t*d,int)override{memcpy(d,shared,32);}
 void onPeerDataRecv(mesh::Packet*,uint8_t,int,const uint8_t*,uint8_t*d,size_t n)override{received.assign(d,d+n);}
 void onAnonDataRecv(mesh::Packet*,const uint8_t*,const mesh::Identity&,uint8_t*d,size_t n)override{anonymous++;received.assign(d,d+n);}
 bool onPeerPathRecv(mesh::Packet*,int,const uint8_t*,uint8_t*,uint8_t,uint8_t,uint8_t*,uint8_t)override{paths++;return false;}
 void onAdvertRecv(mesh::Packet*,const mesh::Identity&,uint32_t,const uint8_t*,size_t)override{adverts++;}
 void receive(mesh::Packet&p){Mesh::onRecvPacket(&p);}
};
mesh::Packet tunnel(const mesh::Packet& p){
 uint8_t raw[255],wire[nwia::kMaxWire],id[6]={1,2,3,4,5,6};auto n=p.writeTo(raw);
 const auto created=p._nwia_mqtt_ingress?p._nwia_mqtt_created:1800000000;assert(created);
 auto w=nwia::encode(wire,id,created,raw,n,nwia::Mode::All);nwia::Envelope e{};
 assert(nwia::decode(wire,w,1800000001,e,nwia::Mode::All));mesh::Packet q;
 assert(q.readFrom(e.raw,e.rawSize));q._nwia_mqtt_ingress=true;q._nwia_mqtt_created=e.created;return q;
}
int main(){
 Radio r;Clock clock;RNG rng;RTC rtc;StaticPoolPacketManager ma(32),mb(32);SimpleMeshTables ta,tb;
 Gateway a(r,clock,rng,rtc,ma,ta,0xa1),b(r,clock,rng,rtc,mb,tb,0xb2);
 // A group flood gains native A and B hops across the link, without touching ciphertext.
 mesh::Packet flood;flood.header=(5<<2)|1;flood.payload_len=19;memset(flood.payload,7,19);
 assert(a.receive(flood)!=ACTION_RELEASE && flood.path[0]==0xa1);
 flood._nwia_mqtt_ingress=true;
 assert(b.receive(flood)!=ACTION_RELEASE && flood.path[1]==0xb2 && flood.getPathHashCount()==2);
 assert(flood.payload[0]==7&&flood._nwia_mqtt_ingress);
 // Reply routes B -> A. Each gateway consumes exactly its own hop.
 mesh::Packet dm;dm.header=(2<<2)|2;dm.path_len=2;dm.path[0]=0xb2;dm.path[1]=0xa1;dm.payload_len=20;memset(dm.payload,0x44,20);
 assert(b.receive(dm)!=ACTION_RELEASE && dm.path_len==1 && dm.path[0]==0xa1);
 dm._nwia_mqtt_ingress=true;
 assert(a.receive(dm)!=ACTION_RELEASE && dm.path_len==0);
 // A direct packet for a different RF repeater is exported, then mirrored intact.
 mesh::Packet foreign;foreign.header=(2<<2)|2;foreign.path_len=1;foreign.path[0]=0xc3;foreign.payload_len=20;memset(foreign.payload,0x55,20);
 assert(a.receive(foreign)==ACTION_RELEASE && a.exports==1);
 foreign._nwia_mqtt_ingress=true;
 assert(b.receive(foreign)!=ACTION_RELEASE && foreign.path_len==1 && foreign.path[0]==0xc3);
 assert(b.receive(foreign)==ACTION_RELEASE); // no repeated mirror
 // Zero-hop ACKs and control/discovery frames must reach local companion radios.
 for(int type:{3,11,15}) {
   mesh::Packet p;p.header=(type<<2)|2;p.payload_len=type==3?4:1;p.payload[0]=type==11?0x80:0x66;
   memset(p.payload+1,0,p.payload_len-1);p._nwia_mqtt_ingress=true;
   assert(a.receive(p)!=ACTION_RELEASE);
 }
 // Forward-disable/scope policy cannot be bypassed by a direct mirror.
 mesh::Packet blocked=foreign;blocked.payload[0]++;
 b.forwarding=false;assert(b.receive(blocked)==ACTION_RELEASE);b.forwarding=true;
 // Trace advances its SNR/path count as a native hop and retains ingress metadata.
 mesh::Packet trace;trace.header=(9<<2)|2;trace.payload_len=11;memset(trace.payload,0,11);trace.payload[9]=0xa1;trace.payload[10]=0xb2;trace._nwia_mqtt_ingress=true;trace._snr=12;
 assert(a.receive(trace)!=ACTION_RELEASE && trace.path_len==1 && trace.path[0]==12);
 assert(b.receive(trace)!=ACTION_RELEASE && trace.path_len==2);
 assert(trace._nwia_mqtt_ingress);
 // Native ACK forwarding allocates a new packet; its no-export origin must survive.
 mesh::Packet ack;ack.header=(3<<2)|2;ack.path_len=1;ack.path[0]=0xb2;ack.payload_len=4;memset(ack.payload,0x77,4);ack._nwia_mqtt_ingress=true;ack._nwia_mqtt_deadline=12345;ack._nwia_mqtt_created=1800000000;
 assert(b.receive(ack)==ACTION_RELEASE);auto forwarded=mb.getNextOutbound(1000);assert(forwarded&&forwarded->_nwia_mqtt_ingress&&forwarded->_nwia_mqtt_deadline==12345&&forwarded->_nwia_mqtt_created==1800000000&&forwarded->_nwia_mqtt_forward&&forwarded->path_len==0);mb.free(forwarded);
 auto reused=mb.allocNew();assert(reused&&!reused->_nwia_mqtt_ingress&&!reused->_nwia_mqtt_deadline&&!reused->_nwia_mqtt_created&&!reused->_nwia_mqtt_forward);mb.free(reused);
 // Expiry survives native queueing and prevents late RF transmission.
 auto expired=ma.allocNew();assert(expired);expired->header=(3<<2)|2;expired->path_len=0;expired->payload_len=4;memset(expired->payload,1,4);
 expired->_nwia_mqtt_ingress=true;expired->_nwia_mqtt_deadline=999;
 ma.queueOutbound(expired,0,1000);clock.now=1001;a.loop();assert(ma.getOutboundTotal()==0&&r.sends==0);
 // A third-site origin can require more than one broker leg. B first
 // mirrors the foreign A/B route, then must accept the shortened B route.
 mesh::Packet multi;multi.header=(2<<2)|2;multi.path_len=2;multi.path[0]=0xa1;multi.path[1]=0xb2;
 multi.payload_len=20;memset(multi.payload,0x95,20);multi._nwia_mqtt_ingress=true;multi._nwia_mqtt_created=1799999980;
 auto firstAtB=multi;assert(b.receive(firstAtB)!=ACTION_RELEASE&&!nwia::mayExport(firstAtB,nwia::Mode::All));
 assert(a.receive(multi)!=ACTION_RELEASE&&multi.path_len==1&&nwia::mayExport(multi,nwia::Mode::All));
 auto nextLeg=tunnel(multi);assert(b.receive(nextLeg)!=ACTION_RELEASE&&nextLeg.path_len==0);
 assert(nwia::mayExport(nextLeg,nwia::Mode::All)&&nextLeg._nwia_mqtt_created==1799999980);
 // Real encrypted endpoints: DMs, management requests and responses across
 // A/B routing, plus anonymous login transport and signed advertisements.
 StaticPoolPacketManager mc(32),md(32);SimpleMeshTables tc,td;
 Endpoint client(r,clock,rng,rtc,mc,tc),remote(r,clock,rng,rtc,md,td);
 client.pair(remote.self_id);remote.pair(client.self_id);
 assert(!memcmp(client.shared,remote.shared,32));
 const uint8_t body[]={0,0xd2,0x49,0x6b,0,'t','e','s','t'};
 for(int type:{PAYLOAD_TYPE_TXT_MSG,PAYLOAD_TYPE_REQ,PAYLOAD_TYPE_RESPONSE}) {
  auto p=client.createDatagram(type,remote.self_id,client.shared,body,sizeof(body));assert(p);
  p->header|=ROUTE_TYPE_DIRECT;p->path_len=2;p->path[0]=0xa1;p->path[1]=0xb2;
  assert(a.receive(*p)!=ACTION_RELEASE);auto q=tunnel(*p);assert(b.receive(q)!=ACTION_RELEASE&&q.path_len==0);
  remote.receive(q);assert(remote.received.size()>=sizeof(body)&&!memcmp(remote.received.data(),body,sizeof(body)));mc.free(p);
 }
 auto login=client.createAnonDatagram(PAYLOAD_TYPE_ANON_REQ,client.self_id,remote.self_id,client.shared,body,sizeof(body));assert(login);
 login->header|=ROUTE_TYPE_FLOOD;assert(a.receive(*login)!=ACTION_RELEASE);auto logged=tunnel(*login);
 assert(b.receive(logged)!=ACTION_RELEASE);remote.receive(logged);assert(remote.anonymous==1);mc.free(login);
 auto response=remote.createDatagram(PAYLOAD_TYPE_RESPONSE,client.self_id,remote.shared,body,sizeof(body));assert(response);
 response->header|=ROUTE_TYPE_DIRECT;response->path_len=2;response->path[0]=0xb2;response->path[1]=0xa1;
 assert(b.receive(*response)!=ACTION_RELEASE);auto back=tunnel(*response);assert(a.receive(back)!=ACTION_RELEASE);
 client.receive(back);assert(!memcmp(client.received.data(),body,sizeof(body)));md.free(response);
 auto advert=client.createAdvert(client.self_id);assert(advert);advert->header|=ROUTE_TYPE_FLOOD;
 assert(a.receive(*advert)!=ACTION_RELEASE);auto ad=tunnel(*advert);assert(b.receive(ad)!=ACTION_RELEASE);
 remote.receive(ad);assert(remote.adverts==1);mc.free(advert);
 auto path=remote.createPathReturn(client.self_id,remote.shared,(const uint8_t*)"AB",2,0,nullptr,0);assert(path);
 path->header|=ROUTE_TYPE_DIRECT;path->path_len=2;path->path[0]=0xb2;path->path[1]=0xa1;
 assert(b.receive(*path)!=ACTION_RELEASE);auto returned=tunnel(*path);assert(a.receive(returned)!=ACTION_RELEASE);
 client.receive(returned);assert(client.paths==1);md.free(path);
 std::cout<<"PASS: real encrypted DM/request/response/login/path and signed advert delivery; actual MeshCore flood hops, bidirectional direct paths, foreign-hop/zero-hop delivery, duplicate/scope checks, trace and ACK ingress metadata\n";
}
