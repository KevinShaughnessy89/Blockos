#include "ndp.hpp"
#include "ethernet.hpp"
#include "ipv6.hpp"
#include <string.h>

namespace blockos::net {
namespace {
struct Entry { bool valid; IPv6Address ip; MacAddress mac; uint32_t age; };
Entry cache[16]{};
IPv6Address g_router{};
uint32_t g_age=0;

#pragma pack(push,1)
struct Icmp6 { uint8_t type,code; uint16_t checksum; uint32_t reserved; uint8_t target[16]; uint8_t opts[0]; };
struct Nda { uint8_t type; uint8_t len; uint8_t mac[6]; };
struct RaFixed { uint8_t type,code; uint16_t checksum; uint8_t hop; uint8_t flags; uint16_t life; uint32_t reach; uint32_t retrans; };
#pragma pack(pop)

bool same(const IPv6Address&a,const IPv6Address&b){return a==b;}
void solicit_mac(const IPv6Address& target,uint8_t mac[6]){ mac[0]=0x33;mac[1]=0x33;mac[2]=0xff;mac[3]=target.b[13];mac[4]=target.b[14];mac[5]=target.b[15]; }
IPv6Address solicited_node(const IPv6Address&t){ return IPv6Address{{0xff,0x02,0,0,0,0,0,1,0xff,t.b[13],t.b[14],t.b[15]}}; }
void put_opt(uint8_t*p,uint8_t type,const uint8_t*data){p[0]=type;p[1]=1;memcpy(p+2,data,6);}
}

void ndp_init(){ memset(cache,0,sizeof(cache)); g_router={}; g_age=0; }

bool ndp_lookup(const IPv6Address& ip, MacAddress& mac){
    for(auto&e:cache) if(e.valid&&same(e.ip,ip)){mac=e.mac;return true;} return false;
}

bool ndp_request(const IPv6Address& target){
    uint8_t packet[32+8]{};
    Icmp6* h=(Icmp6*)packet; h->type=135; h->code=0; h->reserved=0; memcpy(h->target,target.b,16);
    uint8_t* opt=packet+20; opt[0]=1; opt[1]=1; memcpy(opt+2,mac_address().b,6);
    const IPv6Address dst=solicited_node(target);
    h->checksum=0; h->checksum=htons(ipv6_transport_checksum(ipv6_local(),dst,58,packet,28));
    return ipv6_send(dst,58,packet,28,255);
}

void ndp_receive(const IPv6Address& src,const IPv6Address& dst,const void*packet,size_t len){
    if(!packet||len<8)return;
    const uint8_t*p=(const uint8_t*)packet;
    const uint8_t type=p[0];
    if(type==135 && len>=24){
        const uint8_t*target=p+8; IPv6Address t{};memcpy(t.b,target,16);
        if(t!=ipv6_local()) return;
        MacAddress m=mac_address();
        IPv6Address outdst=src; if(ipv6_is_unspecified(src)) outdst=ipv6_all_nodes_multicast();
        uint8_t reply[32]{}; Icmp6*h=(Icmp6*)reply;h->type=136;h->code=0;h->reserved=0;memcpy(h->target,ipv6_local().b,16);
        uint8_t*o=reply+20;o[0]=2;o[1]=1;memcpy(o+2,m.b,6);
        h->checksum=htons(ipv6_transport_checksum(ipv6_local(),outdst,58,reply,28));
        ipv6_send(outdst,58,reply,28,255);
        return;
    }
    if(type==136 && len>=24){
        IPv6Address t{};memcpy(t.b,p+8,16);if(t!=ipv6_local())return;
        const uint8_t*o=p+24;size_t n=len-24;for(size_t i=0;i+8<=n;i+=8){if(o[i]==2&&o[i+1]==1){Entry*slot=nullptr;for(auto&e:cache)if(e.valid&&e.ip==t){slot=&e;break;}if(!slot)for(auto&e:cache)if(!e.valid){slot=&e;break;}if(slot){slot->valid=true;slot->ip=t;memcpy(slot->mac.b,o+i+2,6);slot->age=g_age;}}}
        return;
    }
    if(type==134 && len>=16){
        const RaFixed*ra=(const RaFixed*)packet;g_router=src;uint16_t life=ntohs(ra->life);(void)life;
        size_t off=16;
        while(off+2<=len){uint8_t ot=p[off],olen=uint8_t(p[off+1])*8;if(olen<8||off+olen>len)break;if(ot==3&&olen>=32){uint8_t plen=p[off+2],flags=p[off+3];if(plen==64&&(flags&0x40)){IPv6Address prefix{};memcpy(prefix.b,p+off+16,16);ipv6_set_prefix(prefix,plen);IPv6Address local=prefix;auto mac=mac_address();local.b[8]=mac.b[0]^0x02;local.b[9]=mac.b[1];local.b[10]=mac.b[2];local.b[11]=0xff;local.b[12]=0xfe;local.b[13]=mac.b[3];local.b[14]=mac.b[4];local.b[15]=mac.b[5];ipv6_set_local(local);}}off+=olen;}
        return;
    }
    (void)dst;
}

IPv6Address ndp_router(){return g_router;}
bool ndp_has_router(){return !ipv6_is_unspecified(g_router);}

} // namespace blockos::net
