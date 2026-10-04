#include "blockos_dns.h"
#include "sys/socket.h"
#include "errno.h"
#include "fcntl.h"
#include "unistd.h"
#include "string.h"
#include <stddef.h>
#include <stdint.h>

static uint16_t bswap16(uint16_t x) { return (uint16_t)((x << 8) | (x >> 8)); }

static uint32_t g_dns_server = 0x01010101u; /* 1.1.1.1 */
static uint16_t g_dns_id = 0x4100;

struct dns_header {
    uint16_t id, flags, qdcount, ancount, nscount, arcount;
} __attribute__((packed));

static int encode_name(const char* name, uint8_t* out, size_t cap, size_t* used) {
    if (!name || !out || !used || !*name) return -1;
    const size_t len = strlen(name);
    if (len > 253) return -1;
    size_t pos = 0, start = 0;
    while (start < len) {
        size_t end = start;
        while (end < len && name[end] != '.') ++end;
        const size_t n = end - start;
        if (!n || n > 63 || pos + 1 + n >= cap) return -1;
        out[pos++] = (uint8_t)n;
        memcpy(out + pos, name + start, n);
        pos += n;
        start = (end < len) ? end + 1 : end;
    }
    if (pos + 1 > cap) return -1;
    out[pos++] = 0;
    *used = pos;
    return 0;
}

static int skip_name(const uint8_t* p, size_t len, size_t* pos) {
    size_t hops = 0;
    while (*pos < len) {
        const uint8_t c = p[(*pos)++];
        if (c == 0) return 0;
        if ((c & 0xC0) == 0xC0) {
            if (*pos >= len) return -1;
            ++*pos;
            return 0;
        }
        if (c > 63 || *pos + c > len) return -1;
        *pos += c;
        if (++hops > 128) return -1;
    }
    return -1;
}

static int parse_ipv4_literal(const char* s, uint32_t* out) {
    if (!s || !out) return -1;
    uint32_t v = 0;
    int parts = 0;
    unsigned part = 0;
    for (size_t i = 0;; ++i) {
        const char c = s[i];
        if (c >= '0' && c <= '9') {
            part = part * 10u + (unsigned)(c - '0');
            if (part > 255u) return -1;
        } else if (c == '.' || c == '\0') {
            if (parts >= 4) return -1;
            v = (v << 8) | part;
            ++parts;
            part = 0;
            if (!c) break;
        } else return -1;
    }
    if (parts != 4) return -1;
    *out = v;
    return 0;
}

int blockos_dns_set_server(uint32_t addr) {
    if (!addr) { errno = EINVAL; return -1; }
    g_dns_server = addr;
    return 0;
}

int blockos_dns_resolve_a(const char* hostname, uint32_t* out_addr) {
    if (!hostname || !out_addr || !*hostname) { errno = EINVAL; return -1; }
    if (parse_ipv4_literal(hostname, out_addr) == 0) return 0;

    int fd = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (fd < 0) return -1;

    uint8_t packet[512];
    memset(packet, 0, sizeof(packet));
    struct dns_header* h = (struct dns_header*)packet;
    ++g_dns_id;
    if (!g_dns_id) ++g_dns_id;
    h->id = bswap16(g_dns_id);
    h->flags = bswap16(0x0100); /* RD */
    h->qdcount = bswap16(1);

    size_t pos = sizeof(*h), used = 0;
    if (encode_name(hostname, packet + pos, sizeof(packet) - pos, &used) != 0) {
        close(fd); errno = EINVAL; return -1;
    }
    pos += used;
    if (pos + 4 > sizeof(packet)) { close(fd); errno = EINVAL; return -1; }
    packet[pos++] = 0; packet[pos++] = 1; /* A */
    packet[pos++] = 0; packet[pos++] = 1; /* IN */

    struct sockaddr_in dns = {0};
    dns.sin_family = AF_INET;
    dns.sin_port = bswap16(53);
    dns.sin_addr.s_addr = g_dns_server;

    int sent = (int)sendto(fd, packet, pos, 0, (const struct sockaddr*)&dns, sizeof(dns));
    if (sent != (int)pos) { close(fd); return -1; }

    for (unsigned attempt = 0; attempt < 200000; ++attempt) {
        uint8_t reply[1500];
        struct sockaddr_in from = {0};
        socklen_t flen = sizeof(from);
        int n = (int)recvfrom(fd, reply, sizeof(reply), 0, (struct sockaddr*)&from, &flen);
        if (n < 0) {
            if (errno == EAGAIN || errno == EINTR) {
                if ((attempt & 0x3ffu) == 0) sched_yield();
                continue;
            }
            break;
        }
        if (n < (int)sizeof(struct dns_header)) continue;
        struct dns_header* rh = (struct dns_header*)reply;
        const uint16_t rid = bswap16(rh->id);
        const uint16_t flags = bswap16(rh->flags);
        const uint16_t qd = bswap16(rh->qdcount);
        const uint16_t an = bswap16(rh->ancount);
        if (rid != g_dns_id || !(flags & 0x8000) || (flags & 0x000F) != 0 || !qd) continue;

        size_t rp = sizeof(*rh);
        int bad = 0;
        for (uint16_t i = 0; i < qd; ++i) {
            if (skip_name(reply, (size_t)n, &rp) != 0 || rp + 4 > (size_t)n) { bad = 1; break; }
            rp += 4;
        }
        if (bad) continue;

        for (uint16_t i = 0; i < an; ++i) {
            if (skip_name(reply, (size_t)n, &rp) != 0 || rp + 10 > (size_t)n) { bad = 1; break; }
            const uint16_t type = ((uint16_t)reply[rp] << 8) | reply[rp + 1];
            const uint16_t cls  = ((uint16_t)reply[rp + 2] << 8) | reply[rp + 3];
            const uint16_t rdlen = ((uint16_t)reply[rp + 8] << 8) | reply[rp + 9];
            rp += 10;
            if (rp + rdlen > (size_t)n) { bad = 1; break; }
            if (type == 1 && cls == 1 && rdlen == 4) {
                *out_addr = ((uint32_t)reply[rp] << 24) |
                            ((uint32_t)reply[rp + 1] << 16) |
                            ((uint32_t)reply[rp + 2] << 8) |
                            ((uint32_t)reply[rp + 3]);
                close(fd);
                return 0;
            }
            rp += rdlen;
        }
        if (bad) continue;
    }

    close(fd);
    errno = EHOSTUNREACH;
    return -1;
}
