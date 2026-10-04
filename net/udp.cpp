#include "udp.hpp"
#include "ipv4.hpp"
#include <string.h>
namespace blockos::net {
struct UdpHeader { uint16_t src,dst,len,checksum; } __attribute__((packed));
struct Binding { bool used; uint16_t port; UdpHandler handler; };
struct Binding6 { bool used; uint16_t port; UdpHandler6 handler; };
static Binding bindings[32]{}; static Binding6 bindings6[32]{};
static uint16_t csum4(const IPv4Address&src,const IPv4Address&dst,const void*p,size_t len){uint32_t s=0;auto a=[&](uint16_t v){s+=v;while(s>>16)s=(s&0xffff)+(s>>16);};for(int i=0;i<4;i+=2){a((src.b[i]<<8)|src.b[i+1]);a((dst.b[i]<<8)|dst.b[i+1]);}a(IP_UDP);a((uint16_t)len);const uint8_t*q=(const uint8_t*)p;while(len>=2){a((q[0]<<8)|q[1]);q+=2;len-=2;}if(len)a(q[0]<<8);return uint16_t(~s);}
void udp_init(){memset(bindings,0,sizeof(bindings));memset(bindings6,0,sizeof(bindings6));}
bool udp_bind(uint16_t port,UdpHandler handler){if(!port||!handler)return false;for(auto&b:bindings)if(b.used&&b.port==port&&b.handler==handler)return true;for(auto&b:bindings)if(!b.used){b={true,port,handler};return true;}return false;}
bool udp_send(const IPv4Address&dst,uint16_t src,uint16_t dport,const void*data,size_t len){uint8_t packet[1480];if(len+8>sizeof(packet)||(!data&&len))return false;auto*h=(UdpHeader*)packet;h->src=htons(src);h->dst=htons(dport);h->len=htons(uint16_t(8+len));h->checksum=0;if(len)memcpy(packet+8,data,len);h->checksum=htons(csum4(ip_address(),dst,packet,8+len));if(h->checksum==0)h->checksum=0xffff;return ipv4_send(dst,IP_UDP,packet,8+len);}
void udp_receive(const IPv4Address&src,const void*packet,size_t len){if(len<8)return;const auto*h=(const UdpHeader*)packet;uint16_t ulen=ntohs(h->len);if(ulen<8||ulen>len)return;if(h->checksum){uint16_t old=h->checksum;UdpHeader tmp=*h;(void)old;tmp.checksum=old;if(csum4(src,ip_address(),packet,ulen)!=0)return;}uint16_t port=ntohs(h->dst);for(const auto&b:bindings)if(b.used&&b.port==port){UdpDatagram d{src,ntohs(h->src),port,(const uint8_t*)packet+8,size_t(ulen-8)};b.handler(d);return;}}
bool udp6_bind(uint16_t port,UdpHandler6 handler){if(!port||!handler)return false;for(auto&b:bindings6)if(b.used&&b.port==port&&b.handler==handler)return true;for(auto&b:bindings6)if(!b.used){b={true,port,handler};return true;}return false;}
bool udp6_send(const IPv6Address&dst,uint16_t src,uint16_t dport,const void*data,size_t len){uint8_t packet[1232];if(len+8>sizeof(packet)||(!data&&len))return false;auto*h=(UdpHeader*)packet;h->src=htons(src);h->dst=htons(dport);h->len=htons(uint16_t(8+len));h->checksum=0;if(len)memcpy(packet+8,data,len);h->checksum=htons(ipv6_transport_checksum(ipv6_local(),dst,17,packet,8+len));if(h->checksum==0)h->checksum=0xffff;return ipv6_send(dst,17,packet,8+len,64);}
void udp6_receive(const IPv6Address&src,const IPv6Address&dst,const void*packet,size_t len){if(len<8)return;const auto*h=(const UdpHeader*)packet;uint16_t ulen=ntohs(h->len);if(ulen<8||ulen>len)return;if(h->checksum==0||ipv6_transport_checksum(src,dst,17,packet,ulen)!=0)return;uint16_t port=ntohs(h->dst);for(const auto&b:bindings6)if(b.used&&b.port==port){UdpDatagram6 d{src,ntohs(h->src),port,(const uint8_t*)packet+8,size_t(ulen-8)};b.handler(d);return;}}
} // namespace blockos::net
