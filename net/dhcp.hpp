#pragma once
#include <stdint.h>
namespace blockos::net::dhcp {
struct Lease { uint32_t ip; uint32_t mask; uint32_t gateway; uint32_t dns; uint32_t lease_seconds; bool valid; };
bool start();
void poll();
Lease lease();
}
