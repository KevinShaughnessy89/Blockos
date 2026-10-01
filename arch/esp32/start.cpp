#include "esp32.hpp"
#include "drivers/gpio.hpp"
#include "drivers/uart.hpp"

namespace esp32_arch {

extern "C" void blockos_arch_start_c()
{
    memory_init();
    irq_disable();
    timer_init(DEFAULT_CPU_HZ, DEFAULT_TICK_HZ);

    esp32::uart::init(0, 115200);
    esp32::gpio::reset_all();

    esp32::uart::puts("\r\n");
    esp32::uart::puts("BlockOS ESP32\r\n");
    esp32::uart::puts("architecture: Xtensa LX6\r\n");
    esp32::uart::puts("boot: ESP32 application image\r\n");
    esp32::uart::puts("\r\n");

    blockos_kernel_main();
    wait_forever();
}

} // namespace esp32_arch
