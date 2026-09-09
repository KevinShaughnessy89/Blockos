#include "net.hpp"
#include "ethernet.hpp"
#include "arp.hpp"
#include "ipv4.hpp"
#include "icmp.hpp"
#include "udp.hpp"
#include "tcp.hpp"
#include "virtio_net_driver.hpp"

#include <string.h>

namespace blockos::net {

static MacAddress g_mac{};
static IPv4Address g_ip{{10,0,2,15}};
static IPv4Address g_mask{{255,255,255,0}};
static IPv4Address g_gw{{10,0,2,2}};

uint16_t checksum(const void* data, size_t len) {
    const uint8_t* p = static_cast<const uint8_t*>(data);
    uint32_t sum = 0;
    while (len >= 2) {
        sum += (uint32_t(p[0]) << 8) | p[1];
        p += 2; len -= 2;
    }
    if (len) sum += uint32_t(p[0]) << 8;
    while (sum >> 16) sum = (sum & 0xffffu) + (sum >> 16);
    return uint16_t(~sum);
}

MacAddress mac_address() { return g_mac; }
IPv4Address ip_address() { return g_ip; }
IPv4Address netmask() { return g_mask; }
IPv4Address gateway() { return g_gw; }

void set_ipv4(const IPv4Address& ip, const IPv4Address& mask, const IPv4Address& gw) {
    g_ip = ip; g_mask = mask; g_gw = gw;
}

bool send_frame(const uint8_t dst[6], uint16_t type,
                const void* payload, size_t payload_len) {
    uint8_t frame[1514];
    if (payload_len + sizeof(EthernetHeader) > sizeof(frame)) return false;
    EthernetHeader h{};
    memcpy(h.dst, dst, 6);
    memcpy(h.src, g_mac.b, 6);
    h.type = htons(type);
    memcpy(frame, &h, sizeof(h));
    memcpy(frame + sizeof(h), payload, payload_len);
    return virtio_net::send_packet(frame, unsigned(sizeof(h) + payload_len));
}

void input_frame(const void* frame, size_t len) {
    ethernet_receive(frame, len);
}

void init() {
    arp_init();
    udp_init();
    tcp_init();
}

void poll() {
    uint8_t packet[2048];
    for (;;) {
        int n = virtio_net::receive_packet(packet, sizeof(packet));
        if (n <= 0) break;
        input_frame(packet, size_t(n));
    }
    tcp_tick();
}

} // namespace blockos::net
