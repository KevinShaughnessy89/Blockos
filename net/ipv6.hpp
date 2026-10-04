#pragma once
#include <stdint.h>
#include <stddef.h>
#include "net.hpp"

namespace blockos::net {

struct IPv6Address {
    uint8_t b[16]{};
    bool operator==(const IPv6Address& o) const;
    bool operator!=(const IPv6Address& o) const { return !(*this == o); }
};

bool ipv6_is_unspecified(const IPv6Address& a);
bool ipv6_is_multicast(const IPv6Address& a);
IPv6Address ipv6_all_nodes_multicast();
IPv6Address ipv6_all_routers_multicast();
IPv6Address ipv6_dhcp_multicast();

void ipv6_init();
void ipv6_receive(const void* packet, size_t len);
bool ipv6_send(const IPv6Address& dst, uint8_t next_header,
               const void* payload, size_t payload_len, uint8_t hop_limit = 64);
bool ipv6_set_local(const IPv6Address& addr);
IPv6Address ipv6_local();
bool ipv6_set_router(const IPv6Address& addr);
IPv6Address ipv6_router();
bool ipv6_set_prefix(const IPv6Address& prefix, uint8_t prefix_len);
uint8_t ipv6_prefix_length();

uint16_t ipv6_transport_checksum(const IPv6Address& src,
                                 const IPv6Address& dst,
                                 uint8_t next_header,
                                 const void* data,
                                 size_t len);

} // namespace blockos::net
