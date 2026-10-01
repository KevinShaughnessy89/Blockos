#include "esp32.hpp"

namespace {

static uint32_t g_cpu_hz = esp32_arch::DEFAULT_CPU_HZ;
static uint32_t g_tick_hz = 1000;
static uint32_t g_cycles_per_tick = 240000;
static uint32_t g_last_cycle = 0;
static uint32_t g_ticks = 0;
static uint64_t g_high_cycles = 0;

static inline uint32_t elapsed(uint32_t now, uint32_t then)
{
    return now - then;
}

} // namespace

namespace esp32_arch {

void timer_init(uint32_t cpu_hz, uint32_t tick_hz)
{
    if (cpu_hz == 0)
        cpu_hz = DEFAULT_CPU_HZ;

    if (tick_hz == 0)
        tick_hz = 1000;

    g_cpu_hz = cpu_hz;
    g_tick_hz = tick_hz;

    g_cycles_per_tick =
        cpu_hz / tick_hz;

    if (g_cycles_per_tick == 0)
        g_cycles_per_tick = 1;

    g_last_cycle = cpu_cycles();
    g_ticks = 0;
    g_high_cycles = 0;
}

bool timer_poll()
{
    const uint32_t now = cpu_cycles();
    const uint32_t delta =
        elapsed(now, g_last_cycle);

    /*
     * Account for CCOUNT wrap naturally with unsigned subtraction.
     */
    if (delta < g_cycles_per_tick)
        return false;

    const uint32_t whole_ticks =
        delta / g_cycles_per_tick;

    g_last_cycle +=
        whole_ticks * g_cycles_per_tick;

    g_ticks += whole_ticks;
    g_high_cycles +=
        static_cast<uint64_t>(delta);

    return true;
}

uint32_t timer_ticks()
{
    return g_ticks;
}

uint64_t timer_micros()
{
    /*
     * CCOUNT is 32-bit. Keep a monotonic accumulator by folding the
     * current delta since the last sampled point into g_high_cycles.
     */
    const uint32_t now = cpu_cycles();
    const uint32_t delta =
        elapsed(now, g_last_cycle);

    const uint64_t cycles =
        g_high_cycles +
        static_cast<uint64_t>(delta);

    return
        (cycles * 1000000ULL) /
        static_cast<uint64_t>(g_cpu_hz);
}

uint32_t timer_cpu_hz()
{
    return g_cpu_hz;
}

uint32_t timer_tick_hz()
{
    return g_tick_hz;
}

} // namespace esp32_arch
