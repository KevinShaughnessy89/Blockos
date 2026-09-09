#include "icmp.hpp"
#include "ipv4.hpp"
#include <string.h>

namespace blockos::net {

struct IcmpHeader {
    uint8_t type;
    uint8_t code;
    uint16_t checksum;
    uint16_t id;
    uint16_t seq;
} __attribute__((packed));

void icmp_receive(const IPv4Address& src, const void* packet, size_t len) {
    if (len < sizeof(IcmpHeader)) return;
    const auto* in = static_cast<const IcmpHeader*>(packet);
    if (checksum(packet, len) != 0) return;
    if (in->type != 8 || in->code != 0) return;

    uint8_t reply[1500];
    if (len > sizeof(reply)) return;
    memcpy(reply, packet, len);
    auto* h = reinterpret_cast<IcmpHeader*>(reply);
    h->type = 0;
    h->checksum = 0;
    h->checksum = checksum(reply, len);
    ipv4_send(src, IP_ICMP, reply, len);
}

bool icmp_echo_request(const IPv4Address& dst, uint16_t id, uint16_t seq,
                       const void* data, size_t len) {
    uint8_t packet[1500];
    if (len + sizeof(IcmpHeader) > sizeof(packet)) return false;
    auto* h = reinterpret_cast<IcmpHeader*>(packet);
    h->type = 8; h->code = 0; h->checksum = 0;
    h->id = htons(id); h->seq = htons(seq);
    memcpy(packet + sizeof(IcmpHeader), data, len);
    h->checksum = checksum(packet, sizeof(IcmpHeader) + len);
    return ipv4_send(dst, IP_ICMP, packet, sizeof(IcmpHeader) + len);
}

} // namespace blockos::net
