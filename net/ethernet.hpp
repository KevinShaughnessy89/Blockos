#pragma once
#include <stdint.h>
#include <stddef.h>
#include "net.hpp"

namespace blockos::net {

constexpr uint16_t ETHERTYPE_IPV4 = 0x0800;
constexpr uint16_t ETHERTYPE_ARP  = 0x0806;

struct EthernetHeader {
    uint8_t dst[6];
    uint8_t src[6];
    uint16_t type;
} __attribute__((packed));

static_assert(sizeof(EthernetHeader) == 14);

void ethernet_receive(const void* frame, size_t len);

} // namespace blockos::net
