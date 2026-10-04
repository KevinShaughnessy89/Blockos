#pragma once
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif

struct blockos_http_client;
struct blockos_http_client* blockos_http_open(const char* host, unsigned short port);
int blockos_http_request(struct blockos_http_client* c,
                         const char* method,
                         const char* path,
                         const char* extra_headers,
                         const void* body,
                         size_t body_len);
long blockos_http_read(struct blockos_http_client* c, void* out, size_t cap);
int blockos_http_status(struct blockos_http_client* c);
int blockos_http_close(struct blockos_http_client* c);

#ifdef __cplusplus
}
#endif
