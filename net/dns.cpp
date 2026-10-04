#include "dns.hpp"
#include "dhcp.hpp"
#include "udp.hpp"
#include "net.hpp"
#include <string.h>

namespace blockos::net::dns {
namespace {

static uint32_t g_server = 0;
static uint16_t g_id = 0x5101;
static volatile bool g_done = false;
static volatile uint32_t g_answer = 0;
static volatile uint8_t g_rcode = 0;
static bool g_bound = false;

static uint16_t rd16(const uint8_t* p) {
    return uint16_t((uint16_t(p[0]) << 8) | p[1]);
}

static uint32_t rd32(const uint8_t* p) {
    return (uint32_t(p[0]) << 24) | (uint32_t(p[1]) << 16) |
           (uint32_t(p[2]) << 8) | uint32_t(p[3]);
}

static bool skip_name(const uint8_t* p, size_t len, size_t& pos) {
    size_t hops = 0;
    while (pos < len) {
        const uint8_t c = p[pos++];
        if (c == 0) return true;
        if ((c & 0xC0) == 0xC0) {
            if (pos >= len) return false;
            ++pos;
            return true;
        }
        if (c > 63 || pos + c > len) return false;
        pos += c;
        if (++hops > 128) return false;
    }
    return false;
}

static bool encode_name(const char* name, uint8_t* out, size_t cap, size_t& used) {
    used = 0;
    if (!name || !*name) return false;

    size_t start = 0;
    const size_t len = strlen(name);
    if (len > 253) return false;

    while (start < len) {
        size_t end = start;
        while (end < len && name[end] != '.') ++end;
        const size_t n = end - start;
        if (!n || n > 63 || used + 1 + n >= cap) return false;
        out[used++] = uint8_t(n);
        memcpy(out + used, name + start, n);
        used += n;
        start = (end < len) ? end + 1 : end;
    }
    if (used + 1 > cap) return false;
    out[used++] = 0;
    return true;
}

static void rx(const UdpDatagram& d) {
    if (d.dst_port != 5300 || !d.data || d.length < 12) return;

    const uint8_t* p = d.data;
    const uint16_t id = rd16(p + 0);
    const uint16_t flags = rd16(p + 2);
    if (id != g_id || !(flags & 0x8000)) return;   // response only

    g_rcode = uint8_t(flags & 0x000F);
    if (g_rcode != 0) {
        g_done = true;
        return;
    }

    const uint16_t qd = rd16(p + 4);
    const uint16_t an = rd16(p + 6);
    const uint16_t ns = rd16(p + 8);
    const uint16_t ar = rd16(p + 10);
    (void)ar;
    if (!qd) return;

    size_t pos = 12;
    for (uint16_t i = 0; i < qd; ++i) {
        if (!skip_name(p, d.length, pos) || pos + 4 > d.length) return;
        pos += 4;
    }

    for (uint16_t i = 0; i < an; ++i) {
        if (!skip_name(p, d.length, pos) || pos + 10 > d.length) return;
        const uint16_t type = rd16(p + pos + 0);
        const uint16_t cls = rd16(p + pos + 2);
        const uint16_t rdlen = rd16(p + pos + 8);
        pos += 10;
        if (pos + rdlen > d.length) return;

        if (type == 1 && cls == 1 && rdlen == 4) {
            g_answer = rd32(p + pos);
            g_done = true;
            return;
        }
        pos += rdlen;
    }

    // A valid reply with no A answer is still terminal for this simple A resolver.
    (void)ns;
    g_done = true;
}

static uint32_t choose_server() {
    if (g_server) return g_server;
    const auto lease = dhcp::lease();
    if (lease.valid && lease.dns) return lease.dns;
    // Public fallback. DHCP still wins when available.
    return 0x01010101u; // 1.1.1.1
}

} // namespace

void set_server(uint32_t ip) {
    g_server = ip;
}

uint32_t server() {
    return choose_server();
}

bool resolve_a(const char* name, uint32_t& out, uint32_t loops) {
    if (!name || !*name || !is_initialized()) return false;

    // Fast path for numeric IPv4 literals.
    uint32_t literal = 0;
    size_t parts = 0;
    size_t value = 0;
    bool valid_literal = true;
    for (size_t i = 0;; ++i) {
        const char c = name[i];
        if (c >= '0' && c <= '9') {
            value = value * 10u + uint32_t(c - '0');
            if (value > 255) { valid_literal = false; break; }
        } else if (c == '.' || c == '\0') {
            if (parts >= 4) { valid_literal = false; break; }
            literal = (literal << 8) | uint32_t(value);
            ++parts;
            value = 0;
            if (c == '\0') break;
        } else {
            valid_literal = false;
            break;
        }
    }
    if (valid_literal && parts == 4) {
        out = literal;
        return true;
    }

    if (!g_bound) {
        if (!udp_bind(5300, rx)) return false;
        g_bound = true;
    }

    uint8_t packet[512]{};
    ++g_id;
    if (!g_id) ++g_id;
    packet[0] = uint8_t(g_id >> 8);
    packet[1] = uint8_t(g_id);
    packet[2] = 0x01; // RD: recursion desired
    packet[5] = 0x01; // QDCOUNT = 1

    size_t pos = 12;
    size_t encoded = 0;
    if (!encode_name(name, packet + pos, sizeof(packet) - pos, encoded)) return false;
    pos += encoded;
    if (pos + 4 > sizeof(packet)) return false;
    packet[pos++] = 0;
    packet[pos++] = 1; // QTYPE A
    packet[pos++] = 0;
    packet[pos++] = 1; // QCLASS IN

    const uint32_t s = choose_server();
    const IPv4Address dst{{uint8_t(s >> 24), uint8_t(s >> 16),
                           uint8_t(s >> 8), uint8_t(s)}};

    g_done = false;
    g_answer = 0;
    g_rcode = 0;
    if (!udp_send(dst, 5300, 53, packet, pos)) return false;

    for (uint32_t i = 0; i < loops && !g_done; ++i) {
        poll();
        if ((i & 0x3FFu) == 0) __asm__ volatile("pause");
    }

    if (!g_done || g_rcode != 0 || !g_answer) return false;
    out = g_answer;
    return true;
}

} // namespace blockos::net::dns
