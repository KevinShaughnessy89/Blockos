#include "esp32.hpp"

namespace {

static uint32_t g_boot_stack = 0;
static uint32_t g_saved_ps = 0;

static inline uint32_t read_ps()
{
    uint32_t value;
    __asm__ volatile(
        "rsr.ps %0"
        : "=a"(value)
    );
    return value;
}

static inline void write_ps(uint32_t value)
{
    __asm__ volatile(
        "wsr.ps %0"
        :
        : "a"(value)
        : "memory"
    );

    __asm__ volatile("rsync");
}

} // namespace

namespace esp32_arch {

uint32_t irq_save()
{
    uint32_t previous;

    /*
     * Xtensa PS.INTLEVEL is raised to level 15, masking normal interrupts.
     * rsil returns the previous PS value.
     */
    __asm__ volatile(
        "rsil %0, 15"
        : "=a"(previous)
        :
        : "memory"
    );

    return previous;
}

void irq_restore(uint32_t ps)
{
    write_ps(ps);
}

void irq_disable()
{
    g_saved_ps = irq_save();
}

void irq_enable()
{
    /*
     * Lower INTLEVEL to zero while retaining other PS bits.
     */
    uint32_t ps = read_ps();
    ps &= ~0x0000000Fu;
    write_ps(ps);
}

void cpu_relax()
{
    __asm__ volatile("nop");
}

void wait_for_interrupt()
{
    __asm__ volatile(
        "waiti 0"
        ::: "memory"
    );
}

uint32_t cpu_cycles()
{
    uint32_t value;

    __asm__ volatile(
        "rsr.ccount %0"
        : "=a"(value)
    );

    return value;
}

void arch_record_boot_stack(void* sp)
{
    g_boot_stack =
        static_cast<uint32_t>(
            reinterpret_cast<uintptr_t>(sp)
        );
}

uint32_t arch_boot_stack()
{
    return g_boot_stack;
}

} // namespace esp32_arch
