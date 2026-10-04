#pragma once
#include <stdint.h>
#include <stddef.h>
#include "ipv6.hpp"

namespace blockos::net {

constexpr uint8_t ICMPV6_ECHO_REQUEST = 128;
constexpr uint8_t ICMPV6_ECHO_REPLY   = 129;

void icmpv6_init();
void icmpv6_receive(const IPv6Address& src, const IPv6Address& dst,
                    const void* packet, size_t len);
bool icmpv6_echo_request(const IPv6Address& dst, uint16_t id, uint16_t seq,
                         const void* data, size_t len);

} // namespace blockos::net
