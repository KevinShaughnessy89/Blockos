#include "dns.hpp"
#include "dhcp.hpp"
#include "udp.hpp"
#include "net.hpp"
#include <string.h>
namespace blockos::net::dns {
namespace {uint32_t g_server=0;uint16_t g_id=0x5101;uint32_t g_answer=0;bool g_done=false;
static uint32_t be32(const uint8_t*p){return (uint32_t(p[0])<<24)|(uint32_t(p[1])<<16)|(uint32_t(p[2])<<8)|p[3];}
static void rx(const UdpDatagram&d){if(d.dst_port!=5300||d.length<12)return;const uint8_t*p=d.data;uint16_t id=uint16_t(p[0]<<8|p[1]);if(id!=g_id)return;uint16_t q=uint16_t(p[4]<<8|p[5]);uint16_t a=uint16_t(p[6]<<8|p[7]);if(!q||!a)return;size_t i=12;while(i<d.length&&p[i]){i+=(size_t)p[i]+1;if(i>d.length)return;}if(i+5>d.length)return;i+=5;for(uint16_t n=0;n<a;n++){if(i>=d.length)return;if((p[i]&0xC0)==0xC0)i+=2;else{while(i<d.length&&p[i]){i+=(size_t)p[i]+1;if(i>d.length)return;}++i;}if(i+10>d.length)return;uint16_t type=uint16_t(p[i]<<8|p[i+1]);uint16_t cls=uint16_t(p[i+2]<<8|p[i+3]);uint16_t rd=uint16_t(p[i+8]<<8|p[i+9]);i+=10;if(i+rd>d.length)return;if(type==1&&cls==1&&rd==4){g_answer=be32(p+i);g_done=true;return;}i+=rd;}}
}
void set_server(uint32_t ip){g_server=ip;}
bool resolve_a(const char*name,uint32_t&out,uint32_t loops){if(!name)return false;if(!g_server){auto l=dhcp::lease();g_server=l.dns?l.dns:0x08080808u;}if(!udp_bind(5300,rx))return false;uint8_t p[512]{};p[0]=uint8_t(g_id>>8);p[1]=uint8_t(g_id);p[2]=1;p[5]=1;size_t pos=12,start=0;size_t len=strlen(name);for(size_t i=0;i<=len;i++)if(name[i]=='.'||name[i]==0){size_t n=i-start;if(!n||n>63||pos+n+1>=sizeof(p))return false;p[pos++]=uint8_t(n);memcpy(p+pos,name+start,n);pos+=n;start=i+1;}p[pos++]=0;p[pos++]=0;p[pos++]=1;p[pos++]=0;p[pos++]=1;g_done=false;g_answer=0;uint32_t s=g_server;IPv4Address dst{{uint8_t(s>>24),uint8_t(s>>16),uint8_t(s>>8),uint8_t(s)}};if(!udp_send(dst,5300,53,p,pos))return false;for(uint32_t i=0;i<loops&&!g_done;i++){}if(!g_done)return false;out=g_answer;return true;}
}
