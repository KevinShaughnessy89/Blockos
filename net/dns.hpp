#pragma once
#include <stdint.h>
#include <stddef.h>
#include "net.hpp"
#include "ipv6.hpp"
namespace blockos::net::dns {

enum RecordType : uint16_t { A=1, CNAME=5, MX=15, TXT=16, AAAA=28 };
struct DnsRecord { RecordType type; uint32_t ttl; IPv4Address a; IPv6Address aaaa; uint16_t mx_priority; char name[128]; char text[256]; };
struct DnsResult { uint16_t count; DnsRecord records[8]; };
void set_server(uint32_t ip);
void set_server6(const IPv6Address& ip);
bool query(const char*name, RecordType type, DnsResult& out, uint32_t timeout_loops=200000);
bool resolve_a(const char*name,uint32_t&out,uint32_t loops=200000);
bool resolve_aaaa(const char*name,IPv6Address&out,uint32_t loops=200000);
bool resolve_cname(const char*name,char*out,size_t out_len,uint32_t loops=200000);
bool resolve_mx(const char*name,uint16_t&priority,char*out,size_t out_len,uint32_t loops=200000);
bool resolve_txt(const char*name,char*out,size_t out_len,uint32_t loops=200000);
}
