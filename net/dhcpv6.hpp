#pragma once
#include <stdint.h>
#include "ipv6.hpp"
namespace blockos::net::dhcpv6 {
struct Lease6 { IPv6Address address; uint32_t preferred_lifetime; uint32_t valid_lifetime; bool valid; };
bool start(uint32_t timeout_loops=200000);
void poll();
Lease6 lease();
}
