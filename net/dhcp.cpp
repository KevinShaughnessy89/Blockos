#include "dhcp.hpp"
#include "udp.hpp"
#include "net.hpp"
#include <string.h>
namespace blockos::net::dhcp {
#pragma pack(push,1)
struct H{uint8_t op,htype,hlen,hops;uint32_t xid;uint16_t secs,flags;uint32_t ciaddr,yiaddr,siaddr,giaddr;uint8_t chaddr[16];uint8_t sname[64];uint8_t file[128];uint8_t cookie[4];};
#pragma pack(pop)
namespace{Lease g{};uint32_t xid=0xB10C0501u;uint8_t mac[6]{};uint32_t server=0;int state=0;bool done=false;uint32_t retry=0;uint32_t be32(const uint8_t*p){return (uint32_t(p[0])<<24)|(uint32_t(p[1])<<16)|(uint32_t(p[2])<<8)|p[3];}void opt(uint8_t*o,size_t&n,uint8_t t,const void*d,uint8_t l){o[n++]=t;o[n++]=l;memcpy(o+n,d,l);n+=l;}
void rx(const UdpDatagram&d){if(d.dst_port!=68||d.length<240)return;const H*h=(const H*)d.data;if(h->op!=2||ntohl(h->xid)!=xid||memcmp(h->chaddr,mac,6))return;uint8_t msg=0,mi=0;server=h->siaddr?ntohl(h->siaddr):0;const uint8_t*o=d.data+240;size_t n=d.length-240;for(size_t i=0;i<n;){uint8_t t=o[i++];if(t==255)break;if(t==0)continue;if(i>=n)break;uint8_t l=o[i++];if(i+l>n)break;if(t==53&&l)msg=o[i];if(t==54&&l==4)server=be32(o+i);if(t==1&&l==4)g.mask=be32(o+i);if(t==3&&l>=4)g.gateway=be32(o+i);if(t==6&&l>=4)g.dns=be32(o+i);if(t==51&&l==4)g.lease_seconds=be32(o+i);(void)mi;i+=l;}if(msg==2&&state==1){g.ip=ntohl(h->yiaddr);uint8_t p[300]{};H*q=(H*)p;q->op=1;q->htype=1;q->hlen=6;q->xid=htonl(xid);q->flags=htons(0x8000);memcpy(q->chaddr,mac,6);q->cookie[0]=99;q->cookie[1]=130;q->cookie[2]=83;q->cookie[3]=99;uint8_t*oo=p+240;size_t z=0;uint8_t m=3;opt(oo,z,53,&m,1);uint32_t gip=g.ip;uint32_t sv=server;opt(oo,z,50,&gip,4);if(server)opt(oo,z,54,&sv,4);oo[z++]=255;udp_send({{255,255,255,255}},68,67,p,240+z);state=2;}else if(msg==5&&(state==1||state==2)){g.ip=ntohl(h->yiaddr);if(!g.mask)g.mask=0xffffff00u;IPv4Address ip{{uint8_t(g.ip>>24),uint8_t(g.ip>>16),uint8_t(g.ip>>8),uint8_t(g.ip)}};IPv4Address mask{{uint8_t(g.mask>>24),uint8_t(g.mask>>16),uint8_t(g.mask>>8),uint8_t(g.mask)}};IPv4Address gw{{uint8_t(g.gateway>>24),uint8_t(g.gateway>>16),uint8_t(g.gateway>>8),uint8_t(g.gateway)}};set_ipv4(ip,mask,gw);g.valid=true;done=true;state=3;}}
}
bool start(){g={};done=false;state=0;retry=0;if(!is_initialized())return false;auto macaddr=mac_address();memcpy(mac,macaddr.b,6);if(!udp_bind(68,rx))return false;uint8_t p[300]{};H*h=(H*)p;h->op=1;h->htype=1;h->hlen=6;h->xid=htonl(xid);h->flags=htons(0x8000);memcpy(h->chaddr,mac,6);h->cookie[0]=99;h->cookie[1]=130;h->cookie[2]=83;h->cookie[3]=99;uint8_t*o=p+240;size_t n=0;uint8_t msgtype=1;opt(o,n,53,&msgtype,1);uint8_t req[]={1,3,6,15,51,54};opt(o,n,55,req,sizeof(req));o[n++]=255;state=1;return udp_send({{255,255,255,255}},68,67,p,240+n);} 
void poll(){if(done)return;if((retry++&0x3ffu)==0&&state==1)start();}
Lease lease(){return g;}
}
