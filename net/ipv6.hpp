#pragma once
#include <stdint.h>
#include <stddef.h>
#include "net.hpp"
namespace blockos::net {
struct IPv6Address { uint8_t b[16]{}; bool operator==(const IPv6Address&o)const; };
void ipv6_init();
void ipv6_receive(const void* packet,size_t len);
bool ipv6_send(const IPv6Address& dst,uint8_t next_header,const void* payload,size_t payload_len,uint8_t hop_limit=64);
bool ipv6_set_local(const IPv6Address& addr);
IPv6Address ipv6_local();
}
