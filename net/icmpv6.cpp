#include "icmpv6.hpp"
#include <string.h>

namespace blockos::net {
namespace {
bool g_ready=false;
}
void icmpv6_init(){g_ready=true;}
void icmpv6_receive(const IPv6Address& src,const IPv6Address& dst,const void*packet,size_t len){
    if(!g_ready||!packet||len<8)return;
    if(ipv6_transport_checksum(src,dst,58,packet,len) != 0) return;
    const uint8_t*p=(const uint8_t*)packet;
    if(p[0]!=ICMPV6_ECHO_REQUEST||p[1]!=0)return;
    uint8_t reply[1232]{};if(len>sizeof(reply))return;memcpy(reply,packet,len);reply[0]=ICMPV6_ECHO_REPLY;*reinterpret_cast<uint16_t*>(reply+2)=0;*reinterpret_cast<uint16_t*>(reply+2)=htons(ipv6_transport_checksum(ipv6_local(),src,58,reply,len));ipv6_send(src,58,reply,len,64);
}
bool icmpv6_echo_request(const IPv6Address&dst,uint16_t id,uint16_t seq,const void*data,size_t len){uint8_t p[1232]{};if(len+8>sizeof(p)||(!data&&len))return false;p[0]=128;p[1]=0;p[4]=uint8_t(id>>8);p[5]=uint8_t(id);p[6]=uint8_t(seq>>8);p[7]=uint8_t(seq);if(len)memcpy(p+8,data,len);*reinterpret_cast<uint16_t*>(p+2)=htons(ipv6_transport_checksum(ipv6_local(),dst,58,p,8+len));return ipv6_send(dst,58,p,8+len,64);}
}
