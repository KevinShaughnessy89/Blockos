#include "esp32.hpp"

namespace {

uint32_t g_cpu_hz = esp32_arch::DEFAULT_CPU_HZ;
uint32_t g_tick_hz = esp32_arch::DEFAULT_TICK_HZ;
uint32_t g_cycles_per_tick = esp32_arch::DEFAULT_CPU_HZ / esp32_arch::DEFAULT_TICK_HZ;
uint32_t g_last_cycle = 0;
uint32_t g_ticks = 0;
uint64_t g_cycles_base = 0;

} // namespace

namespace esp32_arch {

void timer_init(uint32_t cpu_hz, uint32_t tick_hz)
{
    if (cpu_hz == 0)
        cpu_hz = DEFAULT_CPU_HZ;
    if (tick_hz == 0)
        tick_hz = DEFAULT_TICK_HZ;

    g_cpu_hz = cpu_hz;
    g_tick_hz = tick_hz;
    g_cycles_per_tick = cpu_hz / tick_hz;
    if (g_cycles_per_tick == 0)
        g_cycles_per_tick = 1;

    g_last_cycle = cpu_cycles();
    g_ticks = 0;
    g_cycles_base = 0;
}

bool timer_poll()
{
    const uint32_t now = cpu_cycles();
    const uint32_t delta = now - g_last_cycle;
    if (delta < g_cycles_per_tick)
        return false;

    const uint32_t whole = delta / g_cycles_per_tick;
    g_last_cycle += whole * g_cycles_per_tick;
    g_ticks += whole;
    g_cycles_base += static_cast<uint64_t>(whole) * g_cycles_per_tick;
    return true;
}

uint32_t timer_ticks()
{
    timer_poll();
    return g_ticks;
}

uint64_t timer_micros()
{
    const uint32_t now = cpu_cycles();
    const uint32_t delta = now - g_last_cycle;
    const uint64_t cycles = g_cycles_base + static_cast<uint64_t>(delta);
    return (cycles * 1000000ULL) / static_cast<uint64_t>(g_cpu_hz);
}

void delay_us(uint32_t us)
{
    const uint32_t start = cpu_cycles();
    const uint64_t cycles_needed =
        (static_cast<uint64_t>(g_cpu_hz) * us) / 1000000ULL;
    while (static_cast<uint32_t>(cpu_cycles() - start) <
           static_cast<uint32_t>(cycles_needed)) {
        cpu_relax();
    }
}

uint32_t timer_cpu_hz() { return g_cpu_hz; }
uint32_t timer_tick_hz() { return g_tick_hz; }

} // namespace esp32_arch
