#pragma once
#include <stdint.h>
#include <stddef.h>
#include "net.hpp"

namespace blockos::net {

void arp_init();
void arp_receive(const void* packet, size_t len);
bool arp_lookup(const IPv4Address& ip, MacAddress& mac);
bool arp_request(const IPv4Address& ip);

} // namespace blockos::net
