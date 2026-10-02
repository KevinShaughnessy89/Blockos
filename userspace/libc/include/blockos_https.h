#pragma once
#include <stddef.h>
#ifdef __cplusplus
extern "C" {
#endif
struct blockos_https_client;
struct blockos_https_client* blockos_https_open(const char* host, unsigned short port);
int blockos_https_request(struct blockos_https_client* c, const char* method, const char* path, const void* body, size_t body_len);
long blockos_https_read(struct blockos_https_client* c, void* out, size_t cap);
int blockos_https_close(struct blockos_https_client* c);
int blockos_https_tls_ready(void);
#ifdef __cplusplus
}
#endif
