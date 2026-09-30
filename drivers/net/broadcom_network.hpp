#pragma once

#include <stdint.h>
#include <stddef.h>

namespace broadcom_network {

bool probe(uint8_t bus, uint8_t slot, uint8_t func);
bool init(uint8_t bus, uint8_t slot, uint8_t func);
bool ready();
bool packet_io_ready();
uint64_t mmio_base();
void get_mac(uint8_t out[6]);
uint16_t vendor_id();
uint16_t device_id();
const char* driver_name();

} // namespace broadcom_network
