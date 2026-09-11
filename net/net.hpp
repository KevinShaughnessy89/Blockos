#pragma once

#include <stdint.h>
#include <stddef.h>

namespace blockos::net {

struct MacAddress {
    uint8_t b[6]{};
};

struct IPv4Address {
    uint8_t b[4]{};

    bool operator==(const IPv4Address& o) const {
        return b[0] == o.b[0] &&
               b[1] == o.b[1] &&
               b[2] == o.b[2] &&
               b[3] == o.b[3];
    }

    bool operator!=(const IPv4Address& o) const {
        return !(*this == o);
    }
};

constexpr uint16_t htons(uint16_t x) {
    return uint16_t((x << 8) | (x >> 8));
}

constexpr uint16_t ntohs(uint16_t x) {
    return htons(x);
}

constexpr uint32_t htonl(uint32_t x) {
    return ((x & 0x000000FFu) << 24) |
           ((x & 0x0000FF00u) << 8)  |
           ((x & 0x00FF0000u) >> 8)  |
           ((x & 0xFF000000u) >> 24);
}

constexpr uint32_t ntohl(uint32_t x) {
    return htonl(x);
}

uint16_t checksum(const void* data, size_t len);

void init();
void poll();

MacAddress mac_address();
IPv4Address ip_address();
IPv4Address netmask();
IPv4Address gateway();

void set_ipv4(const IPv4Address& ip,
              const IPv4Address& mask,
              const IPv4Address& gw);

bool send_frame(const uint8_t dst[6],
                uint16_t ethertype,
                const void* payload,
                size_t payload_len);

void input_frame(const void* frame, size_t len);

} // namespace blockos::net
