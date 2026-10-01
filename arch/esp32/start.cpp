#include "esp32.hpp"

namespace esp32_arch {

extern void arch_record_boot_stack(void* sp);

extern "C" void blockos_arch_start_c(void* boot_stack)
{
    /*
     * Clear .bss before touching architecture-layer globals.
     */
    memory_init();

    arch_record_boot_stack(boot_stack);

    /*
     * The classic ESP32 can boot on the PRO CPU while the APP CPU is held
     * in reset until the multicore startup path explicitly releases it.
     * This initial architecture port therefore starts as a single-CPU
     * target and leaves multicore startup for the ESP32 SMP layer.
     */
    irq_disable();
    timer_init(DEFAULT_CPU_HZ, 1000);

    if (blockos_kernel_main) {
        blockos_kernel_main();
    }

    for (;;) {
        wait_for_interrupt();
    }
}

} // namespace esp32_arch
