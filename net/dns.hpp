#pragma once
#include <stdint.h>
namespace blockos::net::dns {
void set_server(uint32_t ip);
bool resolve_a(const char* name,uint32_t& out,uint32_t timeout_loops=200000);
}
