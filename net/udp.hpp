#pragma once
#include <stdint.h>
#include <stddef.h>
#include "net.hpp"
#include "ipv6.hpp"

namespace blockos::net {

struct UdpDatagram {
    IPv4Address src;
    uint16_t src_port;
    uint16_t dst_port;
    const uint8_t* data;
    size_t length;
};
using UdpHandler = void(*)(const UdpDatagram&);

void udp_init();
bool udp_bind(uint16_t port, UdpHandler handler);
bool udp_send(const IPv4Address& dst, uint16_t src_port, uint16_t dst_port,
              const void* data, size_t len);
void udp_receive(const IPv4Address& src, const void* packet, size_t len);

struct UdpDatagram6 {
    IPv6Address src;
    uint16_t src_port;
    uint16_t dst_port;
    const uint8_t* data;
    size_t length;
};
using UdpHandler6 = void(*)(const UdpDatagram6&);

bool udp6_bind(uint16_t port, UdpHandler6 handler);
bool udp6_send(const IPv6Address& dst, uint16_t src_port, uint16_t dst_port,
               const void* data, size_t len);
void udp6_receive(const IPv6Address& src, const IPv6Address& dst,
                  const void* packet, size_t len);

} // namespace blockos::net
