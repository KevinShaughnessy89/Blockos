#pragma once
#include <stdint.h>
#include <stddef.h>
#include "ipv6.hpp"

namespace blockos::net {

void ndp_init();
bool ndp_lookup(const IPv6Address& ip, MacAddress& mac);
bool ndp_request(const IPv6Address& target);
void ndp_receive(const IPv6Address& src, const IPv6Address& dst,
                 const void* packet, size_t len);
IPv6Address ndp_router();
bool ndp_has_router();

} // namespace blockos::net
