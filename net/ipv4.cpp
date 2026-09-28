#include "ipv4.hpp"
#include "ethernet.hpp"
#include "arp.hpp"
#include "icmp.hpp"
#include "udp.hpp"
#include "tcp.hpp"
#include <string.h>

namespace blockos::net {

static uint16_t identification = 1;

static bool all_zero(const IPv4Address& a)
{
    return a.b[0] == 0 && a.b[1] == 0 && a.b[2] == 0 && a.b[3] == 0;
}

static bool is_broadcast(const IPv4Address& a)
{
    return a.b[0] == 255 && a.b[1] == 255 &&
           a.b[2] == 255 && a.b[3] == 255;
}

static bool same_subnet(const IPv4Address& a, const IPv4Address& b)
{
    const auto m = netmask();
    if (all_zero(m))
        return false;
    for (int i = 0; i < 4; ++i)
        if ((a.b[i] & m.b[i]) != (b.b[i] & m.b[i]))
            return false;
    return true;
}

void ipv4_receive(const void* packet, size_t len)
{
    if (len < sizeof(IPv4Header)) return;
    const auto* h = static_cast<const IPv4Header*>(packet);
    const uint8_t version = h->version_ihl >> 4;
    const uint8_t ihl = uint8_t((h->version_ihl & 0x0f) * 4);
    if (version != 4 || ihl < 20 || ihl > len) return;

    const uint16_t total = ntohs(h->total_length);
    if (total < ihl || total > len) return;

    IPv4Header copy = *h;
    if (checksum(&copy, ihl) != 0) return;

    const IPv4Address dst{{h->dst[0], h->dst[1], h->dst[2], h->dst[3]}};
    const IPv4Address local = ip_address();
    const bool dhcp_broadcast = all_zero(local) && is_broadcast(dst) && h->protocol == IP_UDP;
    if (!dhcp_broadcast && dst != local)
        return;

    const IPv4Address src{{h->src[0], h->src[1], h->src[2], h->src[3]}};
    const uint8_t* p = static_cast<const uint8_t*>(packet) + ihl;
    const size_t plen = total - ihl;

    switch (h->protocol) {
        case IP_ICMP: icmp_receive(src, p, plen); break;
        case IP_UDP:  udp_receive(src, p, plen); break;
        case IP_TCP:  tcp_receive(src, p, plen); break;
        default: break;
    }
}

bool ipv4_send(const IPv4Address& dst, uint8_t protocol,
               const void* payload, size_t payload_len)
{
    if (payload_len > 1480 || (payload_len && !payload))
        return false;

    const IPv4Address src = ip_address();
    MacAddress mac{};

    if (is_broadcast(dst)) {
        memset(mac.b, 0xFF, sizeof(mac.b));
    } else {
        if (all_zero(src))
            return false;
        const IPv4Address next = same_subnet(src, dst) ? dst : gateway();
        if (all_zero(next) || !arp_lookup(next, mac)) {
            if (!all_zero(next))
                arp_request(next);
            return false;
        }
    }

    uint8_t packet[1500]{};
    auto* h = reinterpret_cast<IPv4Header*>(packet);
    h->version_ihl = 0x45;
    h->dscp_ecn = 0;
    h->total_length = htons(uint16_t(sizeof(IPv4Header) + payload_len));
    h->identification = htons(identification++);
    h->flags_fragment = htons(0x4000);
    h->ttl = 64;
    h->protocol = protocol;
    h->checksum = 0;
    memcpy(h->src, src.b, 4);
    memcpy(h->dst, dst.b, 4);
    h->checksum = checksum(h, sizeof(IPv4Header));
    if (payload_len)
        memcpy(packet + sizeof(IPv4Header), payload, payload_len);

    return send_frame(mac.b, ETHERTYPE_IPV4, packet,
                      sizeof(IPv4Header) + payload_len);
}

} // namespace blockos::net
