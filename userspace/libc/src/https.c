#include "blockos_https.h"
#include "blockos_tls_client.h"
#include "blockos_dns.h"
#include "sys/socket.h"
#include "errno.h"
#include "string.h"
#include "stdio.h"
#include "unistd.h"
#include <stdint.h>
#include <stddef.h>
#include <stdlib.h>

__attribute__((weak)) int blockos_mbedtls_install(void);

struct blockos_https_client {
    int fd;
    void *tls;
    unsigned short port;
    char host[256];
};

static struct blockos_tls_client_ops g_tls;

int blockos_tls_register_backend(const struct blockos_tls_client_ops* ops) {
    if (!ops || !ops->handshake || !ops->write || !ops->read || !ops->close) return -1;
    g_tls = *ops;
    return 0;
}

int blockos_tls_backend_available(void) {
    return g_tls.handshake && g_tls.write && g_tls.read && g_tls.close;
}

int blockos_https_tls_ready(void) {
    if (!blockos_tls_backend_available() && blockos_mbedtls_install)
        (void)blockos_mbedtls_install();
    return blockos_tls_backend_available();
}

static unsigned short bswap16(unsigned short x) { return (unsigned short)((x << 8) | (x >> 8)); }

static int tls_write_all(void* tls, const void* data, size_t len) {
    const long n = g_tls.write(tls, data, len);
    return n == (long)len ? 0 : -1;
}

struct blockos_https_client* blockos_https_open(const char* host, unsigned short port) {
    if (!host || !*host) { errno = EINVAL; return 0; }
    if (!blockos_https_tls_ready()) { errno = ENOSYS; return 0; }

    uint32_t ip = 0;
    if (blockos_dns_resolve_a(host, &ip) != 0) return 0;

    struct blockos_https_client* c = calloc(1, sizeof(*c));
    if (!c) { errno = ENOMEM; return 0; }
    c->fd = -1;
    c->port = port ? port : 443;
    strncpy(c->host, host, sizeof(c->host) - 1);

    c->fd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (c->fd < 0) { free(c); return 0; }

    struct sockaddr_in a = {0};
    a.sin_family = AF_INET;
    a.sin_port = bswap16(c->port);
    a.sin_addr.s_addr = ip;
    if (connect(c->fd, (const struct sockaddr*)&a, sizeof(a)) < 0) {
        close(c->fd); free(c); return 0;
    }

    if (g_tls.handshake(&c->tls, c->fd, c->host) != 0) {
        close(c->fd); free(c); return 0;
    }
    return c;
}

int blockos_https_request(struct blockos_https_client* c,
                          const char* method,
                          const char* path,
                          const void* body,
                          size_t body_len) {
    if (!c || !c->tls || !method || !path) { errno = EINVAL; return -1; }
    char h[2048];
    int n = snprintf(h, sizeof(h),
                     "%s %s HTTP/1.1\r\nHost: %s\r\nConnection: close\r\n",
                     method, path, c->host);
    if (n < 0 || (size_t)n >= sizeof(h)) { errno = EINVAL; return -1; }
    size_t used = (size_t)n;
    if (body && body_len) {
        n = snprintf(h + used, sizeof(h) - used,
                     "Content-Length: %u\r\nContent-Type: application/octet-stream\r\n",
                     (unsigned)body_len);
        if (n < 0 || used + (size_t)n >= sizeof(h)) { errno = EINVAL; return -1; }
        used += (size_t)n;
    }
    if (used + 2 >= sizeof(h)) { errno = EINVAL; return -1; }
    h[used++]='\r'; h[used++]='\n';
    if (tls_write_all(c->tls, h, used) != 0) return -1;
    if (body && body_len && tls_write_all(c->tls, body, body_len) != 0) return -1;
    return 0;
}

long blockos_https_read(struct blockos_https_client* c, void* out, size_t cap) {
    if (!c || !c->tls || !out || !cap) { errno = EINVAL; return -1; }
    return g_tls.read(c->tls, out, cap);
}

int blockos_https_close(struct blockos_https_client* c) {
    if (!c) return -1;
    int r = 0;
    if (c->tls) r = g_tls.close(c->tls);
    if (c->fd >= 0) close(c->fd);
    free(c);
    return r;
}
