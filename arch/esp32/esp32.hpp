#pragma once

#include <stdint.h>
#include <stddef.h>

namespace esp32_arch {

constexpr uint32_t DEFAULT_CPU_HZ = 240000000u;

/* CPU/interrupt primitives. */
uint32_t irq_save();
void irq_restore(uint32_t ps);
void irq_disable();
void irq_enable();
void cpu_relax();
void wait_for_interrupt();
uint32_t cpu_cycles();

/* Timer. */
void timer_init(uint32_t cpu_hz = DEFAULT_CPU_HZ, uint32_t tick_hz = 1000);
uint32_t timer_ticks();
uint64_t timer_micros();
bool timer_poll();
uint32_t timer_cpu_hz();
uint32_t timer_tick_hz();

/* Basic memory information. */
uintptr_t ram_begin();
uintptr_t ram_end();
uintptr_t stack_top();
void memory_init();

/*
 * Architecture entry called by the ESP32 application-image entry point.
 *
 * The ROM bootloader loads the second-stage bootloader, which loads the
 * application image and transfers control to its entry point. BlockOS uses
 * that mechanism instead of replacing the immutable ROM reset vector.
 */
extern "C" void blockos_arch_entry(void* boot_stack);

/*
 * Kernel hook supplied by the generic BlockOS kernel.
 *
 * This weak declaration lets the ESP32 architecture layer build before the
 * generic kernel entry is wired into the ESP32 target.
 */
extern "C" void blockos_kernel_main() __attribute__((weak));

} // namespace esp32_arch
