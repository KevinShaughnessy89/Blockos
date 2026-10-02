#pragma once

#ifdef __cplusplus
extern "C" {
#endif

/* Installs a real Mbed TLS backend into blockos_tls_client. */
int blockos_mbedtls_install(void);
int blockos_mbedtls_backend_available(void);
int blockos_mbedtls_set_ca_file(const char* path);

#ifdef __cplusplus
}
#endif
