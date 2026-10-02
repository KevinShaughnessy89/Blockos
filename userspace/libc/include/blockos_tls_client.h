#pragma once
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
struct blockos_tls_client_ops {
    int (*handshake)(void** state, int fd, const char* hostname);
    long (*write)(void* state, const void* data, size_t len);
    long (*read)(void* state, void* data, size_t len);
    int (*close)(void* state);
};
int blockos_tls_register_backend(const struct blockos_tls_client_ops* ops);
int blockos_tls_backend_available(void);
#ifdef __cplusplus
}
#endif
