#include "esp32.hpp"

namespace {

uint32_t g_interrupt_mask = 0;
uint32_t g_interrupt_count = 0;

static inline uint32_t read_intenable()
{
    uint32_t value;
    __asm__ volatile("rsr.intenable %0" : "=a"(value));
    return value;
}

static inline void write_intenable(uint32_t value)
{
    __asm__ volatile("wsr.intenable %0" :: "a"(value) : "memory");
    __asm__ volatile("rsync" ::: "memory");
}

} // namespace

namespace esp32_arch {

uint32_t interrupt_mask()
{
    return read_intenable();
}

void interrupt_set_mask(uint32_t mask)
{
    g_interrupt_mask = mask;
    write_intenable(mask);
}

void interrupt_enable_mask(uint32_t mask)
{
    interrupt_set_mask(read_intenable() | mask);
}

void interrupt_disable_mask(uint32_t mask)
{
    interrupt_set_mask(read_intenable() & ~mask);
}

} // namespace esp32_arch
