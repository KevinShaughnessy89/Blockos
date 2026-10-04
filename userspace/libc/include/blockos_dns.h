#pragma once
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif

/* Resolve hostname to an IPv4 address in network byte order. */
int blockos_dns_resolve_a(const char* hostname, uint32_t* out_addr);
int blockos_dns_set_server(uint32_t addr);

#ifdef __cplusplus
}
#endif
