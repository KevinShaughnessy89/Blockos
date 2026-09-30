#pragma once

#include <stdint.h>
#include <stddef.h>

namespace network_hw {

struct DeviceInfo {
    bool present;
    bool packet_io;
    uint16_t vendor;
    uint16_t device;
    uint8_t bus;
    uint8_t slot;
    uint8_t func;
    uint64_t mmio;
    char driver[32];
    uint8_t mac[6];
};

bool init();
bool ready();
bool send(const uint8_t* data, size_t length);
bool receive(uint8_t* buffer, size_t capacity, size_t* out_length);
void get_mac(uint8_t out[6]);
const DeviceInfo* info();

} // namespace network_hw
