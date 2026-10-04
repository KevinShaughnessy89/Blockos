#include "blockos_http.h"
#include "blockos_dns.h"
#include "sys/socket.h"
#include "errno.h"
#include "string.h"
#include "stdio.h"
#include "unistd.h"
#include <stdint.h>
#include <stddef.h>
#include <stdlib.h>

struct blockos_http_client {
    int fd;
    unsigned short port;
    char host[256];
    int status;
    int header_done;
    char header_buf[8192];
    size_t header_len;
    size_t header_pos;
};

static unsigned short bswap16(unsigned short x) { return (unsigned short)((x << 8) | (x >> 8)); }

static int write_all(int fd, const char* p, size_t n) {
    size_t off = 0;
    while (off < n) {
        long rc = send(fd, p + off, n - off, 0);
        if (rc <= 0) return -1;
        off += (size_t)rc;
    }
    return 0;
}

static int parse_status(struct blockos_http_client* c) {
    if (c->header_len < 12) return -1;
    size_t i = 0;
    while (i + 1 < c->header_len) {
        if (c->header_buf[i] == '\r' && c->header_buf[i + 1] == '\n') break;
        ++i;
    }
    c->header_buf[i < c->header_len ? i : c->header_len - 1] = 0;
    if (strncmp(c->header_buf, "HTTP/", 5) != 0) return -1;
    char* p = strchr(c->header_buf, ' ');
    if (!p) return -1;
    while (*p == ' ') ++p;
    int code = 0;
    for (int j = 0; j < 3 && p[j] >= '0' && p[j] <= '9'; ++j)
        code = code * 10 + (p[j] - '0');
    if (code < 100 || code > 999) return -1;
    c->status = code;
    return 0;
}

struct blockos_http_client* blockos_http_open(const char* host, unsigned short port) {
    if (!host || !*host) { errno = EINVAL; return NULL; }
    struct blockos_http_client* c = calloc(1, sizeof(*c));
    if (!c) { errno = ENOMEM; return NULL; }

    uint32_t ip = 0;
    if (blockos_dns_resolve_a(host, &ip) != 0) { free(c); return NULL; }

    c->fd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (c->fd < 0) { free(c); return NULL; }
    struct sockaddr_in a = {0};
    a.sin_family = AF_INET;
    a.sin_port = bswap16(port ? port : 80);
    a.sin_addr.s_addr = ip;
    if (connect(c->fd, (const struct sockaddr*)&a, sizeof(a)) < 0) {
        close(c->fd); free(c); return NULL;
    }
    strncpy(c->host, host, sizeof(c->host) - 1);
    c->port = port ? port : 80;
    return c;
}

int blockos_http_request(struct blockos_http_client* c,
                         const char* method,
                         const char* path,
                         const char* extra_headers,
                         const void* body,
                         size_t body_len) {
    if (!c || !method || !path) { errno = EINVAL; return -1; }
    char req[4096];
    int n = snprintf(req, sizeof(req), "%s %s HTTP/1.1\r\nHost: %s\r\nConnection: close\r\n",
                     method, path, c->host);
    if (n < 0 || (size_t)n >= sizeof(req)) { errno = EINVAL; return -1; }
    size_t used = (size_t)n;
    if (extra_headers && *extra_headers) {
        const size_t h = strlen(extra_headers);
        if (used + h + 2 >= sizeof(req)) { errno = EINVAL; return -1; }
        memcpy(req + used, extra_headers, h); used += h;
        if (used < 2 || req[used - 2] != '\r' || req[used - 1] != '\n') {
            req[used++] = '\r'; req[used++] = '\n';
        }
    }
    if (body && body_len) {
        n = snprintf(req + used, sizeof(req) - used, "Content-Length: %u\r\n", (unsigned)body_len);
        if (n < 0 || used + (size_t)n >= sizeof(req)) { errno = EINVAL; return -1; }
        used += (size_t)n;
    }
    if (used + 2 >= sizeof(req)) { errno = EINVAL; return -1; }
    req[used++] = '\r'; req[used++] = '\n';

    if (write_all(c->fd, req, used) != 0) return -1;
    if (body && body_len && write_all(c->fd, (const char*)body, body_len) != 0) return -1;
    c->header_done = 0;
    c->header_len = 0;
    c->header_pos = 0;
    c->status = 0;
    return 0;
}

static long raw_read(struct blockos_http_client* c, void* out, size_t cap) {
    return recv(c->fd, out, cap, 0);
}

long blockos_http_read(struct blockos_http_client* c, void* out, size_t cap) {
    if (!c || !out || !cap) { errno = EINVAL; return -1; }
    if (c->header_done) return raw_read(c, out, cap);

    while (c->header_len < sizeof(c->header_buf)) {
        long n = recv(c->fd, c->header_buf + c->header_len,
                      sizeof(c->header_buf) - c->header_len - 1, 0);
        if (n <= 0) return n;
        c->header_len += (size_t)n;
        c->header_buf[c->header_len] = 0;
        for (size_t i = 3; i < c->header_len; ++i) {
            if (c->header_buf[i-3]=='\r' && c->header_buf[i-2]=='\n' &&
                c->header_buf[i-1]=='\r' && c->header_buf[i]=='\n') {
                const size_t body_start = i + 1;
                c->header_pos = body_start;
                c->header_done = 1;
                if (parse_status(c) != 0) { errno = EIO; return -1; }
                const size_t available = c->header_len - body_start;
                const size_t take = available < cap ? available : cap;
                if (take) memcpy(out, c->header_buf + body_start, take);
                if (take < available) c->header_pos += take;
                else c->header_pos = c->header_len;
                return (long)take;
            }
        }
    }
    errno = EIO;
    return -1;
}

int blockos_http_status(struct blockos_http_client* c) {
    return c ? c->status : -1;
}

int blockos_http_close(struct blockos_http_client* c) {
    if (!c) return -1;
    int r = c->fd >= 0 ? close(c->fd) : 0;
    free(c);
    return r;
}
