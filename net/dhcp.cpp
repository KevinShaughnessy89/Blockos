#include "dhcp.hpp"
#include "udp.hpp"
#include "net.hpp"
#include <string.h>
namespace blockos::net::dhcp {
namespace {
struct H { uint8_t op,htype,hlen,hops;uint32_t xid;uint16_t secs,flags;uint32_t ciaddr,yiaddr,siaddr,giaddr;uint8_t chaddr[16];uint8_t sname[64];uint8_t file[128];uint8_t cookie[4]; } __attribute__((packed));
Lease g{};uint32_t xid=0xB10C0501u;bool active=false,received=false;uint8_t mac[6]{};
static uint32_t be32(const uint8_t*p){return (uint32_t(p[0])<<24)|(uint32_t(p[1])<<16)|(uint32_t(p[2])<<8)|p[3];}
static void rx(const UdpDatagram&d){if(d.dst_port!=68||d.length<sizeof(H)-4)return;const H*h=(const H*)d.data;if(h->xid!=xid||h->op!=2)return;g.ip=htonl(h->yiaddr);g.mask=0;g.gateway=0;g.dns=0;g.lease_seconds=0;const uint8_t*o=d.data+240;size_t n=d.length>240?d.length-240:0;for(size_t i=0;i<n;){uint8_t t=o[i++];if(t==255)break;if(t==0)continue;if(i>=n)break;uint8_t l=o[i++];if(i+l>n)break; if(t==1&&l==4)g.mask=be32(o+i);if(t==3&&l>=4)g.gateway=be32(o+i);if(t==6&&l>=4)g.dns=be32(o+i);if(t==51&&l==4)g.lease_seconds=be32(o+i);i+=l;}set_ipv4({{uint8_t(g.ip>>24),uint8_t(g.ip>>16),uint8_t(g.ip>>8),uint8_t(g.ip)}} , {{uint8_t(g.mask>>24),uint8_t(g.mask>>16),uint8_t(g.mask>>8),uint8_t(g.mask)}}, {{uint8_t(g.gateway>>24),uint8_t(g.gateway>>16),uint8_t(g.gateway>>8),uint8_t(g.gateway)}});g.valid=true;received=true;active=false;}
}
bool start(){g={};if(!is_initialized())return false;auto m=mac_address();memcpy(mac,m.b,6);if(!udp_bind(68,rx))return false;uint8_t p[300]{};H*h=(H*)p;h->op=1;h->htype=1;h->hlen=6;h->xid=htonl(xid);h->flags=htons(0x8000);memcpy(h->chaddr,mac,6);h->cookie[0]=99;h->cookie[1]=130;h->cookie[2]=83;h->cookie[3]=99;size_t o=240;p[o++]=53;p[o++]=1;p[o++]=1;p[o++]=55;p[o++]=4;p[o++]=1;p[o++]=3;p[o++]=6;p[o++]=15;p[o++]=255;set_ipv4({{0,0,0,0}},{{0,0,0,0}},{{0,0,0,0}});active=true;return udp_send({{255,255,255,255}},68,67,p,o);}
void poll(){(void)active;(void)received;}
Lease lease(){return g;}
}
