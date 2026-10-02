#pragma once
#include <stdint.h>
#include <stddef.h>

namespace ahci {
struct PortInfo { uint64_t base; uint8_t port; bool implemented; bool disk; };
bool init();
bool available();
size_t ports(PortInfo* out, size_t cap);
bool read(uint8_t port, uint64_t lba, uint16_t sectors, void* buffer);
bool write(uint8_t port, uint64_t lba, uint16_t sectors, const void* buffer);
}
