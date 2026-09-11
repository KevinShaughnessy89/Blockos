#pragma once
#include <stdint.h>
#include <stddef.h>
#include "net.hpp"

namespace blockos::net {

struct IPv4Header {
    uint8_t version_ihl;
    uint8_t dscp_ecn;
    uint16_t total_length;
    uint16_t identification;
    uint16_t flags_fragment;
    uint8_t ttl;
    uint8_t protocol;
    uint16_t checksum;
    uint8_t src[4];
    uint8_t dst[4];
} __attribute__((packed));

static_assert(sizeof(IPv4Header) == 20);

constexpr uint8_t IP_ICMP = 1;
constexpr uint8_t IP_TCP  = 6;
constexpr uint8_t IP_UDP  = 17;

void ipv4_receive(const void* packet, size_t len);
bool ipv4_send(const IPv4Address& dst, uint8_t protocol,
               const void* payload, size_t payload_len);

} // namespace blockos::net
