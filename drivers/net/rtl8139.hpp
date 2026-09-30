#pragma once

#include <stdint.h>
#include <stddef.h>

namespace rtl8139 {

bool probe(uint8_t bus, uint8_t slot, uint8_t func);
bool init(uint8_t bus, uint8_t slot, uint8_t func);
bool ready();
bool send(const uint8_t* data, size_t length);
bool receive(uint8_t* buffer, size_t capacity, size_t* out_length);
void get_mac(uint8_t out[6]);
uint16_t vendor_id();
uint16_t device_id();

} // namespace rtl8139
