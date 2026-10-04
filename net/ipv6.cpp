#include "ipv6.hpp"
#include "ndp.hpp"
#include "icmpv6.hpp"
#include "udp.hpp"
#include <string.h>

namespace blockos::net {
namespace {
IPv6Address g_local{{0xfe,0x80,0,0,0,0,0,0,0x02,0x54,0xff,0xfe,0,0,0x01}};
IPv6Address g_router{};
IPv6Address g_prefix{};
uint8_t g_prefix_len = 64;

#pragma pack(push,1)
struct H {
    uint32_t vtcfl;
    uint16_t len;
    uint8_t next;
    uint8_t hop;
    uint8_t src[16];
    uint8_t dst[16];
};
#pragma pack(pop)

static bool address_eq(const uint8_t* a, const uint8_t* b) { return memcmp(a,b,16)==0; }

static void multicast_mac(const IPv6Address& a, uint8_t mac[6]) {
    mac[0]=0x33; mac[1]=0x33;
    memcpy(mac+2, a.b+12, 4);
}
}

bool IPv6Address::operator==(const IPv6Address& o) const { return address_eq(b,o.b); }
bool ipv6_is_unspecified(const IPv6Address& a) { static const uint8_t z[16]{}; return memcmp(a.b,z,16)==0; }
bool ipv6_is_multicast(const IPv6Address& a) { return a.b[0] == 0xff; }
IPv6Address ipv6_all_nodes_multicast() { return IPv6Address{{0xff,0x02,0,0,0,0,0,0,0,0,0,0,0,0,0,1}}; }
IPv6Address ipv6_all_routers_multicast() { return IPv6Address{{0xff,0x02,0,0,0,0,0,0,0,0,0,0,0,0,0,2}}; }
IPv6Address ipv6_dhcp_multicast() { return IPv6Address{{0xff,0x02,0,0,0,0,0,0,0,0,0,0,0,0,1,2}}; }

void ipv6_init() {
    ndp_init();
    icmpv6_init();
    g_router = {};
}

IPv6Address ipv6_local() { return g_local; }
bool ipv6_set_local(const IPv6Address& a) { g_local=a; return true; }
bool ipv6_set_router(const IPv6Address& a) { g_router=a; return true; }
IPv6Address ipv6_router() { return g_router; }
bool ipv6_set_prefix(const IPv6Address& p, uint8_t len) { if (len>128) return false; g_prefix=p; g_prefix_len=len; return true; }
uint8_t ipv6_prefix_length() { return g_prefix_len; }

uint16_t ipv6_transport_checksum(const IPv6Address& src,
                                 const IPv6Address& dst,
                                 uint8_t next_header,
                                 const void* data,
                                 size_t len) {
    uint32_t sum=0;
    auto add16=[&](uint16_t v){ sum += v; while(sum>>16) sum=(sum&0xffffu)+(sum>>16); };
    for(int i=0;i<16;i+=2) add16((uint16_t(src.b[i])<<8)|src.b[i+1]);
    for(int i=0;i<16;i+=2) add16((uint16_t(dst.b[i])<<8)|dst.b[i+1]);
    add16(uint16_t((len>>16)&0xffff)); add16(uint16_t(len&0xffff));
    add16(uint16_t(next_header));
    const uint8_t* p=static_cast<const uint8_t*>(data);
    while(len>=2){add16((uint16_t(p[0])<<8)|p[1]);p+=2;len-=2;}
    if(len) add16(uint16_t(p[0])<<8);
    return uint16_t(~sum);
}

void ipv6_receive(const void* packet, size_t len) {
    if (!packet || len < sizeof(H)) return;
    const H* h=static_cast<const H*>(packet);
    if ((ntohl(h->vtcfl)>>28)!=6) return;
    const size_t plen=ntohs(h->len);
    if (plen > len-sizeof(H)) return;
    IPv6Address src{},dst{};
    memcpy(src.b,h->src,16); memcpy(dst.b,h->dst,16);
    const uint8_t* p=static_cast<const uint8_t*>(packet)+sizeof(H);
    switch(h->next){
        case 58: icmpv6_receive(src,dst,p,plen); break;
        case 17: udp6_receive(src,dst,p,plen); break;
        default: break;
    }
}

bool ipv6_send(const IPv6Address& dst,uint8_t next,const void*payload,size_t n,uint8_t hop) {
    if(n>1232 || (!payload && n)) return false;
    uint8_t pkt[1280]{}; H* h=(H*)pkt;
    h->vtcfl=htonl(6u<<28); h->len=htons(uint16_t(n)); h->next=next; h->hop=hop;
    memcpy(h->src,g_local.b,16); memcpy(h->dst,dst.b,16);
    if(n) memcpy(pkt+sizeof(H),payload,n);
    uint8_t mac[6]{};
    if(ipv6_is_multicast(dst)) multicast_mac(dst,mac);
    else {
        IPv6Address next_hop=dst;
        const bool has_prefix = (g_prefix_len>0);
        bool on_link = !has_prefix;
        if (has_prefix) {
            const size_t full=g_prefix_len/8; const uint8_t rem=uint8_t(g_prefix_len%8);
            on_link = true;
            for(size_t i=0;i<full;i++) if(dst.b[i]!=g_prefix.b[i]) {on_link=false;break;}
            if(on_link&&rem) { const uint8_t mask=uint8_t(0xffu<<(8-rem)); if((dst.b[full]&mask)!=(g_prefix.b[full]&mask)) on_link=false; }
        }
        if(!on_link&&!ipv6_is_unspecified(g_router)) next_hop=g_router;
        MacAddress m{};
        if(!ndp_lookup(next_hop,m)){ if(!ndp_request(next_hop)) return false; return false; }
        memcpy(mac,m.b,6);
    }
    return send_frame(mac,0x86DD,pkt,sizeof(H)+n);
}

} // namespace blockos::net
