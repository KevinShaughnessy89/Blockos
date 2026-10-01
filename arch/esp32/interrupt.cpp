#include "esp32.hpp"

namespace {

static volatile uint32_t g_interrupt_count = 0;
static uint32_t g_intenable = 0;

static inline uint32_t read_intenable()
{
    uint32_t value;
    __asm__ volatile(
        "rsr.intenable %0"
        : "=a"(value)
    );
    return value;
}

static inline void write_intenable(uint32_t value)
{
    __asm__ volatile(
        "wsr.intenable %0"
        :
        : "a"(value)
        : "memory"
    );

    __asm__ volatile("rsync");
}

static inline uint32_t read_exccause()
{
    uint32_t value;
    __asm__ volatile(
        "rsr.exccause %0"
        : "=a"(value)
    );
    return value;
}

} // namespace

namespace esp32_arch {

/*
 * Enable a selected CPU interrupt mask. The generic BlockOS interrupt
 * dispatcher can later extend this with the ESP32 interrupt matrix.
 */
uint32_t interrupt_mask()
{
    return read_intenable();
}

void interrupt_set_mask(uint32_t mask)
{
    g_intenable = mask;
    write_intenable(mask);
}

void interrupt_enable_mask(uint32_t mask)
{
    const uint32_t current =
        read_intenable();

    interrupt_set_mask(
        current | mask
    );
}

void interrupt_disable_mask(uint32_t mask)
{
    const uint32_t current =
        read_intenable();

    interrupt_set_mask(
        current & ~mask
    );
}

uint32_t interrupt_count()
{
    return g_interrupt_count;
}

void interrupt_account()
{
    ++g_interrupt_count;
}

uint32_t exception_cause()
{
    return read_exccause();
}

} // namespace esp32_arch
