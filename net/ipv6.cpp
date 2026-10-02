#include "ipv6.hpp"
#include <string.h>
namespace blockos::net {
namespace { IPv6Address g_local{{0xfe,0x80,0,0,0,0,0,0,0x02,0x54,0xff,0xfe,0,0,0x01}}; }
bool IPv6Address::operator==(const IPv6Address&o)const{return memcmp(b,o.b,16)==0;}
void ipv6_init(){}
IPv6Address ipv6_local(){return g_local;}
bool ipv6_set_local(const IPv6Address&a){g_local=a;return true;}
#pragma pack(push,1)
struct H{uint32_t vtcfl;uint16_t len;uint8_t next;uint8_t hop;uint8_t src[16];uint8_t dst[16];};
#pragma pack(pop)
void ipv6_receive(const void*p,size_t len){if(!p||len<sizeof(H))return;const H*h=(const H*)p;if((ntohl(h->vtcfl)>>28)!=6)return;uint16_t plen=ntohs(h->len);if(plen>len-sizeof(H))return;}
bool ipv6_send(const IPv6Address&dst,uint8_t next,const void*payload,size_t n,uint8_t hop){if(n>1232||(!payload&&n))return false;uint8_t pkt[1280]{};H*h=(H*)pkt;h->vtcfl=htonl(6u<<28);h->len=htons((uint16_t)n);h->next=next;h->hop=hop;memcpy(h->src,g_local.b,16);memcpy(h->dst,dst.b,16);if(n)memcpy(pkt+sizeof(H),payload,n);uint8_t mac[6]{0x33,0x33,dst.b[12],dst.b[13],dst.b[14],dst.b[15]};if(dst.b[0]==0xff)return send_frame(mac,0x86DD,pkt,sizeof(H)+n);return false;}
}
