#pragma once

#include <stddef.h>
#include <stdint.h>

namespace esp32_arch {

constexpr uint32_t DEFAULT_CPU_HZ = 240000000u;
constexpr uint32_t DEFAULT_APB_HZ = 80000000u;
constexpr uint32_t DEFAULT_TICK_HZ = 1000u;

uint32_t irq_save();
void irq_restore(uint32_t ps);
void irq_disable();
void irq_enable();
void cpu_relax();
[[noreturn]] void wait_forever();
uint32_t cpu_cycles();

void timer_init(uint32_t cpu_hz = DEFAULT_CPU_HZ,
                uint32_t tick_hz = DEFAULT_TICK_HZ);
bool timer_poll();
uint32_t timer_ticks();
uint64_t timer_micros();
void delay_us(uint32_t us);
uint32_t timer_cpu_hz();
uint32_t timer_tick_hz();

void memory_init();
uintptr_t heap_begin();
uintptr_t heap_end();
uintptr_t stack_top();

uint32_t interrupt_mask();
void interrupt_set_mask(uint32_t mask);
void interrupt_enable_mask(uint32_t mask);
void interrupt_disable_mask(uint32_t mask);

extern "C" void blockos_arch_entry();
extern "C" void blockos_arch_start_c();
extern "C" void blockos_kernel_main();

} // namespace esp32_arch
