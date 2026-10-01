#pragma once

#include <stdint.h>

namespace esp32::mmio {

inline volatile uint32_t& reg(uintptr_t address)
{
    return *reinterpret_cast<volatile uint32_t*>(address);
}

inline uint32_t read(uintptr_t address)
{
    return reg(address);
}

inline void write(uintptr_t address, uint32_t value)
{
    reg(address) = value;
}

inline void set_bits(uintptr_t address, uint32_t mask)
{
    reg(address) |= mask;
}

inline void clear_bits(uintptr_t address, uint32_t mask)
{
    reg(address) &= ~mask;
}

} // namespace esp32::mmio
