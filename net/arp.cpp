#include "arp.hpp"
#include "ethernet.hpp"
#include <string.h>

namespace blockos::net {

struct ArpPacket {
    uint16_t htype;
    uint16_t ptype;
    uint8_t hlen;
    uint8_t plen;
    uint16_t oper;
    uint8_t sha[6];
    uint8_t spa[4];
    uint8_t tha[6];
    uint8_t tpa[4];
} __attribute__((packed));

static_assert(sizeof(ArpPacket) == 28);

struct Entry {
    bool valid;
    IPv4Address ip;
    MacAddress mac;
};

static Entry cache[16]{};

void arp_init() {
    memset(cache, 0, sizeof(cache));
}

static void learn(const IPv4Address& ip, const uint8_t mac[6]) {
    int free_i = -1;
    for (int i=0;i<16;i++) {
        if (cache[i].valid && cache[i].ip == ip) {
            memcpy(cache[i].mac.b, mac, 6);
            return;
        }
        if (!cache[i].valid && free_i < 0) free_i = i;
    }
    if (free_i >= 0) {
        cache[free_i].valid = true;
        cache[free_i].ip = ip;
        memcpy(cache[free_i].mac.b, mac, 6);
    }
}

bool arp_lookup(const IPv4Address& ip, MacAddress& mac) {
    for (const auto& e : cache)
        if (e.valid && e.ip == ip) { mac = e.mac; return true; }
    return false;
}

bool arp_request(const IPv4Address& ip) {
    ArpPacket p{};
    p.htype = htons(1);
    p.ptype = htons(ETHERTYPE_IPV4);
    p.hlen = 6; p.plen = 4;
    p.oper = htons(1);
    auto me = mac_address();
    auto mine = ip_address();
    memcpy(p.sha, me.b, 6);
    memcpy(p.spa, mine.b, 4);
    memcpy(p.tpa, ip.b, 4);
    uint8_t ff[6] = {255,255,255,255,255,255};
    return send_frame(ff, ETHERTYPE_ARP, &p, sizeof(p));
}

void arp_receive(const void* packet, size_t len) {
    if (len < sizeof(ArpPacket)) return;
    const auto* p = static_cast<const ArpPacket*>(packet);
    if (ntohs(p->htype) != 1 || ntohs(p->ptype) != ETHERTYPE_IPV4 ||
        p->hlen != 6 || p->plen != 4) return;

    IPv4Address sender{{p->spa[0],p->spa[1],p->spa[2],p->spa[3]}};
    IPv4Address target{{p->tpa[0],p->tpa[1],p->tpa[2],p->tpa[3]}};
    learn(sender, p->sha);

    if (ntohs(p->oper) != 1 || target != ip_address()) return;

    ArpPacket reply{};
    reply.htype = htons(1);
    reply.ptype = htons(ETHERTYPE_IPV4);
    reply.hlen = 6; reply.plen = 4;
    reply.oper = htons(2);
    auto me = mac_address();
    auto mine = ip_address();
    memcpy(reply.sha, me.b, 6);
    memcpy(reply.spa, mine.b, 4);
    memcpy(reply.tha, p->sha, 6);
    memcpy(reply.tpa, p->spa, 4);

    send_frame(p->sha, ETHERTYPE_ARP, &reply, sizeof(reply));
}

} // namespace blockos::net
