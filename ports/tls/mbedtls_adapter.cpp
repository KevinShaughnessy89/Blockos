#include "mbedtls_adapter.hpp"

#if defined(__has_include)
#  if __has_include(<mbedtls/version.h>)
#    define BLOCKOS_HAVE_MBEDTLS 1
#  endif
#endif

#if BLOCKOS_HAVE_MBEDTLS

#include <mbedtls/version.h>
#include <mbedtls/ssl.h>
#include <mbedtls/entropy.h>
#include <mbedtls/ctr_drbg.h>
#include <mbedtls/x509_crt.h>
#include <mbedtls/error.h>

#include "blockos_tls_client.h"
#include <errno.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#if MBEDTLS_VERSION_MAJOR == 3
#  if MBEDTLS_VERSION_NUMBER < 0x03060700u
#    error "BlockOS requires Mbed TLS 3.6.7+ for the TLS backend"
#  endif
#elif MBEDTLS_VERSION_MAJOR >= 4
#  if MBEDTLS_VERSION_NUMBER < 0x04010200u
#    error "BlockOS requires Mbed TLS 4.1.2+ for the TLS backend"
#  endif
#endif

struct MbedTlsState {
    int fd;
    mbedtls_ssl_context ssl;
    mbedtls_ssl_config conf;
    mbedtls_entropy_context entropy;
    mbedtls_ctr_drbg_context drbg;
    mbedtls_x509_crt ca;
    unsigned char* ca_pem;
    size_t ca_len;
};

static char g_ca_file[256] = "/System/etc/ssl/certs/ca-certificates.crt";
static bool g_installed = false;

static int bio_send(void* ctx, const unsigned char* buf, size_t len) {
    const int fd = *(const int*)ctx;
    const ssize_t n = write(fd, buf, len);
    if (n > 0) return (int)n;
    if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK))
        return MBEDTLS_ERR_SSL_WANT_WRITE;
    if (n < 0 && errno == EINTR)
        return MBEDTLS_ERR_SSL_WANT_WRITE;
    return MBEDTLS_ERR_NET_SEND_FAILED;
}

static int bio_recv(void* ctx, unsigned char* buf, size_t len) {
    const int fd = *(const int*)ctx;
    const ssize_t n = read(fd, buf, len);
    if (n > 0) return (int)n;
    if (n == 0) return MBEDTLS_ERR_SSL_PEER_CLOSE_NOTIFY;
    if (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR)
        return MBEDTLS_ERR_SSL_WANT_READ;
    return MBEDTLS_ERR_NET_RECV_FAILED;
}

static int read_ca_file(MbedTlsState* s) {
    FILE* f = fopen(g_ca_file, "rb");
    if (!f) f = fopen("/etc/ssl/certs/ca-certificates.crt", "rb");
    if (!f) return -1;

    if (fseek(f, 0, SEEK_END) != 0) { fclose(f); return -1; }
    const long end = ftell(f);
    if (end <= 0 || end > (4L << 20)) { fclose(f); return -1; }
    rewind(f);

    s->ca_len = (size_t)end;
    s->ca_pem = (unsigned char*)malloc(s->ca_len + 1);
    if (!s->ca_pem) { fclose(f); return -1; }
    const size_t got = fread(s->ca_pem, 1, s->ca_len, f);
    fclose(f);
    if (got != s->ca_len) return -1;
    s->ca_pem[s->ca_len] = 0;

    return mbedtls_x509_crt_parse(&s->ca, s->ca_pem, s->ca_len + 1);
}

static int handshake(void** out, int fd, const char* hostname) {
    if (!out || fd < 0 || !hostname || !*hostname)
        return -1;

    MbedTlsState* s = (MbedTlsState*)calloc(1, sizeof(MbedTlsState));
    if (!s) return -1;
    s->fd = fd;
    mbedtls_ssl_init(&s->ssl);
    mbedtls_ssl_config_init(&s->conf);
    mbedtls_entropy_init(&s->entropy);
    mbedtls_ctr_drbg_init(&s->drbg);
    mbedtls_x509_crt_init(&s->ca);

    int rc = mbedtls_ctr_drbg_seed(
        &s->drbg,
        mbedtls_entropy_func,
        &s->entropy,
        (const unsigned char*)"BlockOS TLS client",
        19);
    if (rc != 0) goto fail;

    rc = read_ca_file(s);
    if (rc != 0) goto fail;

    rc = mbedtls_ssl_config_defaults(
        &s->conf,
        MBEDTLS_SSL_IS_CLIENT,
        MBEDTLS_SSL_TRANSPORT_STREAM,
        MBEDTLS_SSL_PRESET_DEFAULT);
    if (rc != 0) goto fail;

    mbedtls_ssl_conf_authmode(&s->conf, MBEDTLS_SSL_VERIFY_REQUIRED);
    mbedtls_ssl_conf_ca_chain(&s->conf, &s->ca, nullptr);
    mbedtls_ssl_conf_rng(&s->conf, mbedtls_ctr_drbg_random, &s->drbg);
    (void)mbedtls_ssl_conf_cert_profile(&s->conf, &mbedtls_x509_crt_profile_default);
    (void)mbedtls_ssl_conf_min_tls_version(&s->conf, MBEDTLS_SSL_VERSION_TLS1_2);
#if defined(MBEDTLS_SSL_VERSION_TLS1_3)
    (void)mbedtls_ssl_conf_max_tls_version(&s->conf, MBEDTLS_SSL_VERSION_TLS1_3);
#else
    (void)mbedtls_ssl_conf_max_tls_version(&s->conf, MBEDTLS_SSL_VERSION_TLS1_2);
#endif

    rc = mbedtls_ssl_setup(&s->ssl, &s->conf);
    if (rc != 0) goto fail;

    rc = mbedtls_ssl_set_hostname(&s->ssl, hostname);
    if (rc != 0) goto fail;

    mbedtls_ssl_set_bio(&s->ssl, &s->fd, bio_send, bio_recv, nullptr);

    for (;;) {
        rc = mbedtls_ssl_handshake(&s->ssl);
        if (rc == 0) break;
        if (rc == MBEDTLS_ERR_SSL_WANT_READ || rc == MBEDTLS_ERR_SSL_WANT_WRITE)
            continue;
        goto fail;
    }

    if (mbedtls_ssl_get_verify_result(&s->ssl) != 0)
        goto fail;

    *out = s;
    return 0;

fail:
    if (s->ca_pem) free(s->ca_pem);
    mbedtls_x509_crt_free(&s->ca);
    mbedtls_ctr_drbg_free(&s->drbg);
    mbedtls_entropy_free(&s->entropy);
    mbedtls_ssl_config_free(&s->conf);
    mbedtls_ssl_free(&s->ssl);
    free(s);
    return -1;
}

static long tls_write(void* state, const void* data, size_t len) {
    if (!state || !data) return -1;
    MbedTlsState* s = (MbedTlsState*)state;
    size_t off = 0;
    while (off < len) {
        const int rc = mbedtls_ssl_write(&s->ssl,
                                         (const unsigned char*)data + off,
                                         len - off);
        if (rc > 0) { off += (size_t)rc; continue; }
        if (rc == MBEDTLS_ERR_SSL_WANT_READ || rc == MBEDTLS_ERR_SSL_WANT_WRITE)
            continue;
        return -1;
    }
    return (long)off;
}

static long tls_read(void* state, void* data, size_t len) {
    if (!state || !data || !len) return -1;
    MbedTlsState* s = (MbedTlsState*)state;
    for (;;) {
        const int rc = mbedtls_ssl_read(&s->ssl, (unsigned char*)data, len);
        if (rc > 0) return rc;
        if (rc == 0 || rc == MBEDTLS_ERR_SSL_PEER_CLOSE_NOTIFY) return 0;
        if (rc == MBEDTLS_ERR_SSL_WANT_READ || rc == MBEDTLS_ERR_SSL_WANT_WRITE)
            continue;
        return -1;
    }
}

static int tls_close(void* state) {
    if (!state) return 0;
    MbedTlsState* s = (MbedTlsState*)state;
    (void)mbedtls_ssl_close_notify(&s->ssl);
    free(s->ca_pem);
    mbedtls_x509_crt_free(&s->ca);
    mbedtls_ctr_drbg_free(&s->drbg);
    mbedtls_entropy_free(&s->entropy);
    mbedtls_ssl_config_free(&s->conf);
    mbedtls_ssl_free(&s->ssl);
    free(s);
    return 0;
}

int blockos_mbedtls_install(void) {
    static const blockos_tls_client_ops ops = {
        handshake, tls_write, tls_read, tls_close
    };
    const int rc = blockos_tls_register_backend(&ops);
    g_installed = (rc == 0);
    return rc;
}

int blockos_mbedtls_backend_available(void) {
    return g_installed ? 1 : 0;
}

int blockos_mbedtls_set_ca_file(const char* path) {
    if (!path || !*path) return -1;
    const size_t n = strlen(path);
    if (n >= sizeof(g_ca_file)) return -1;
    memcpy(g_ca_file, path, n + 1);
    return 0;
}

#else

int blockos_mbedtls_install(void) { return -1; }
int blockos_mbedtls_backend_available(void) { return 0; }
int blockos_mbedtls_set_ca_file(const char*) { return -1; }

#endif
