#include "udp.hpp"
#include "ipv4.hpp"
#include <string.h>

namespace blockos::net {

struct UdpHeader {
    uint16_t src;
    uint16_t dst;
    uint16_t len;
    uint16_t checksum;
} __attribute__((packed));

struct Binding { bool used; uint16_t port; UdpHandler handler; };
static Binding bindings[32]{};

void udp_init() { memset(bindings, 0, sizeof(bindings)); }

bool udp_bind(uint16_t port, UdpHandler handler) {
    if (!port || !handler) return false;
    for (auto& b : bindings) if (b.used && b.port == port) return false;
    for (auto& b : bindings) if (!b.used) {
        b = {true, port, handler}; return true;
    }
    return false;
}

bool udp_send(const IPv4Address& dst, uint16_t src_port, uint16_t dst_port,
              const void* data, size_t len) {
    uint8_t packet[1480];
    if (len + sizeof(UdpHeader) > sizeof(packet)) return false;
    auto* h = reinterpret_cast<UdpHeader*>(packet);
    h->src = htons(src_port);
    h->dst = htons(dst_port);
    h->len = htons(uint16_t(sizeof(UdpHeader) + len));
    h->checksum = 0;
    memcpy(packet + sizeof(UdpHeader), data, len);
    return ipv4_send(dst, IP_UDP, packet, sizeof(UdpHeader) + len);
}

void udp_receive(const IPv4Address& src, const void* packet, size_t len) {
    if (len < sizeof(UdpHeader)) return;
    const auto* h = static_cast<const UdpHeader*>(packet);
    uint16_t ulen = ntohs(h->len);
    if (ulen < sizeof(UdpHeader) || ulen > len) return;

    uint16_t port = ntohs(h->dst);
    for (const auto& b : bindings) {
        if (b.used && b.port == port) {
            UdpDatagram d{src, ntohs(h->src), port,
                          static_cast<const uint8_t*>(packet) + sizeof(UdpHeader),
                          ulen - sizeof(UdpHeader)};
            b.handler(d);
            return;
        }
    }
}

} // namespace blockos::net
