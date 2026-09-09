#pragma once
#include <stdint.h>
#include <stddef.h>
#include "net.hpp"

namespace blockos::net {
void icmp_receive(const IPv4Address& src, const void* packet, size_t len);
bool icmp_echo_request(const IPv4Address& dst, uint16_t id, uint16_t seq,
                       const void* data, size_t len);
} // namespace blockos::net
