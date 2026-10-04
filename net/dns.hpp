#pragma once
#include <stdint.h>
#include <stddef.h>

namespace blockos::net::dns {

// Configure a DNS server in network-byte-order IPv4 form (e.g. 1.1.1.1 -> 0x01010101).
void set_server(uint32_t ip);
uint32_t server();

// Resolve an A record. The resolver accepts ordinary hostnames and returns
// the first IPv4 address found in network-byte-order form.
bool resolve_a(const char* name, uint32_t& out, uint32_t timeout_loops = 200000);

} // namespace blockos::net::dns
