#include "uart.hpp"
#include "../esp32.hpp"
#include "mmio.hpp"

namespace {

constexpr uintptr_t UART0 = 0x3FF40000u;
constexpr uintptr_t UART1 = 0x3FF50000u;
constexpr uintptr_t UART2 = 0x3FF6E000u;

constexpr uintptr_t FIFO = 0x00;
constexpr uintptr_t INT_ENA = 0x0C;
constexpr uintptr_t CLKDIV = 0x14;
constexpr uintptr_t STATUS = 0x1C;
constexpr uintptr_t CONF0 = 0x20;

uintptr_t g_base = UART0;

uintptr_t base_for_port(unsigned port)
{
    if (port == 1) return UART1;
    if (port == 2) return UART2;
    return UART0;
}

} // namespace

namespace esp32::uart {

bool init(unsigned port, uint32_t baud)
{
    if (baud == 0)
        baud = 115200;
    if (port > 2)
        return false;

    g_base = base_for_port(port);

    /* Classic ESP32 UART clock divider: APB / baud, 4 fractional bits. */
    const uint32_t divider = (esp32_arch::DEFAULT_APB_HZ << 4) / baud;
    mmio::write(g_base + CLKDIV, divider);
    mmio::write(g_base + CONF0, 0u); /* 8 data bits, no parity, 1 stop. */
    mmio::write(g_base + INT_ENA, 0u);
    return true;
}

bool rx_ready()
{
    return (mmio::read(g_base + STATUS) & 0xFFu) != 0;
}

void putc(char c)
{
    while (((mmio::read(g_base + STATUS) >> 16) & 0xFFu) >= 126u)
        esp32_arch::cpu_relax();
    mmio::write(g_base + FIFO, static_cast<uint8_t>(c));
}

void puts(const char* text)
{
    if (!text) return;
    while (*text) {
        if (*text == '\n')
            putc('\r');
        putc(*text++);
    }
}

int read_byte()
{
    if (!rx_ready())
        return -1;
    return static_cast<int>(mmio::read(g_base + FIFO) & 0xFFu);
}

} // namespace esp32::uart
