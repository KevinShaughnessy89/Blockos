#include "dhcpv6.hpp"
#include "udp.hpp"
#include "net.hpp"
#include <string.h>
namespace blockos::net::dhcpv6 {
namespace {
Lease6 g{};uint32_t xid=0x123456;uint8_t mac[6]{};bool replied=false;bool advertised=false;uint8_t duid[10]{};uint8_t server_id[256]{};size_t server_len=0;
void put24(uint8_t*p,uint32_t v){p[0]=uint8_t(v>>16);p[1]=uint8_t(v>>8);p[2]=uint8_t(v);}uint32_t get24(const uint8_t*p){return (uint32_t(p[0])<<16)|(uint32_t(p[1])<<8)|p[2];}
void opt(uint8_t*p,size_t&n,uint16_t t,const void*d,uint16_t l){p[n++]=uint8_t(t>>8);p[n++]=uint8_t(t);p[n++]=uint8_t(l>>8);p[n++]=uint8_t(l);memcpy(p+n,d,l);n+=l;}
void rx(const UdpDatagram6&d){if(d.dst_port!=546||d.length<4)return;const uint8_t*p=d.data;uint8_t type=p[0];if(get24(p+1)!=xid)return;size_t i=4;const uint8_t*iaaddr=nullptr;uint32_t pref=0,valid=0;while(i+4<=d.length){uint16_t t=uint16_t(p[i]<<8|p[i+1]),l=uint16_t(p[i+2]<<8|p[i+3]);i+=4;if(i+l>d.length)break;if(t==2&&l<=sizeof(server_id)){memcpy(server_id,p+i,l);server_len=l;}if(t==5&&l>=24){iaaddr=p+i;pref=(uint32_t(p[i+16])<<24)|(uint32_t(p[i+17])<<16)|(uint32_t(p[i+18])<<8)|p[i+19];valid=(uint32_t(p[i+20])<<24)|(uint32_t(p[i+21])<<16)|(uint32_t(p[i+22])<<8)|p[i+23];}i+=l;}if(type==2){advertised=true;return;}if(type==7&&iaaddr){memcpy(g.address.b,iaaddr,16);g.preferred_lifetime=pref;g.valid_lifetime=valid;g.valid=true;ipv6_set_local(g.address);replied=true;}}
void make_duid(){auto m=mac_address();memcpy(mac,m.b,6);duid[0]=0;duid[1]=3;duid[2]=0;duid[3]=1;memcpy(duid+4,mac,6);}
bool send_msg(uint8_t type){uint8_t p[512]{};p[0]=type;put24(p+1,xid);size_t n=4;opt(p,n,1,duid,sizeof(duid));if(server_len)opt(p,n,2,server_id,(uint16_t)server_len);uint8_t iana[12]{};uint32_t iaid=0xB10C0001u;iana[0]=uint8_t(iaid>>24);iana[1]=uint8_t(iaid>>16);iana[2]=uint8_t(iaid>>8);iana[3]=uint8_t(iaid);opt(p,n,3,iana,sizeof(iana));return udp6_send(ipv6_dhcp_multicast(),546,547,p,n);}
}
bool start(uint32_t timeout_loops){g={};replied=false;advertised=false;server_len=0;make_duid();if(!udp6_bind(546,rx))return false;if(!send_msg(1))return false;for(uint32_t i=0;i<timeout_loops&&!advertised&&!replied;i++){}if(!advertised&&!replied)return false;if(replied)return true;if(!send_msg(3))return false;for(uint32_t i=0;i<timeout_loops&&!replied;i++){}return replied;}
void poll(){ }
Lease6 lease(){return g;}
}
