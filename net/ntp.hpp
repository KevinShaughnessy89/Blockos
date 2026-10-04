#pragma once
#include <stdint.h>
#include "net.hpp"
namespace blockos::net::ntp {
bool sync(const IPv4Address& server, uint64_t& unix_seconds, uint32_t timeout_loops=200000);
bool sync_name(const char* host, uint64_t& unix_seconds, uint32_t timeout_loops=200000);
bool synchronized();
uint64_t unix_time_seconds();
}
