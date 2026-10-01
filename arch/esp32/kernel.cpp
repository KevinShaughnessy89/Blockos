#include "esp32.hpp"
#include "drivers/gpio.hpp"
#include "drivers/i2c.hpp"
#include "drivers/spi.hpp"
#include "drivers/uart.hpp"

namespace {

char g_line[128];
size_t g_line_len = 0;

static void puts(const char* s)
{
    esp32::uart::puts(s);
}

static bool streq(const char* a, const char* b)
{
    while (*a && *b && *a == *b) {
        ++a;
        ++b;
    }
    return *a == 0 && *b == 0;
}

static size_t parse_uint(const char* s, bool& ok)
{
    ok = false;
    size_t value = 0;
    if (!s || !*s)
        return 0;
    while (*s) {
        if (*s < '0' || *s > '9')
            return 0;
        value = value * 10u + static_cast<size_t>(*s - '0');
        ++s;
    }
    ok = true;
    return value;
}

static void execute(char* line)
{
    while (*line == ' ' || *line == '\t')
        ++line;

    if (streq(line, "help")) {
        puts("commands:\r\n");
        puts("  help       show commands\r\n");
        puts("  info       show platform information\r\n");
        puts("  uptime     show microseconds since boot\r\n");
        puts("  gpio N     read GPIO N\r\n");
        puts("  gpio N V   write GPIO N to 0 or 1\r\n");
        puts("  spi-test   run internal SPI loopback test\r\n");
        puts("  i2c-stop   generate an I2C STOP condition\r\n");
        return;
    }

    if (streq(line, "info")) {
        puts("BlockOS ESP32 platform\r\n");
        puts("CPU: Xtensa LX6\r\n");
        puts("CPU clock: 240000000 Hz\r\n");
        puts("APB clock: 80000000 Hz\r\n");
        puts("boot image: ESP32 .bin\r\n");
        return;
    }

    if (streq(line, "uptime")) {
        const uint64_t us = esp32_arch::timer_micros();
        char buf[32];
        size_t p = sizeof(buf) - 1;
        buf[p] = 0;
        if (us == 0) {
            buf[--p] = '0';
        } else {
            uint64_t n = us;
            while (n != 0 && p != 0) {
                buf[--p] = static_cast<char>('0' + (n % 10u));
                n /= 10u;
            }
        }
        puts(&buf[p]);
        puts(" us\r\n");
        return;
    }

    if (line[0] == 'g' && line[1] == 'p' && line[2] == 'i' && line[3] == 'o') {
        const char* p = line + 4;
        while (*p == ' ' || *p == '\t') ++p;
        bool ok = false;
        size_t pin = parse_uint(p, ok);
        if (!ok || pin >= 40) {
            puts("usage: gpio N [0|1]\r\n");
            return;
        }
        while (*p >= '0' && *p <= '9') ++p;
        while (*p == ' ' || *p == '\t') ++p;
        if (*p == 0) {
            puts(esp32::gpio::read(static_cast<unsigned>(pin)) ? "1\r\n" : "0\r\n");
            return;
        }
        bool value_ok = false;
        size_t value = parse_uint(p, value_ok);
        if (!value_ok || value > 1) {
            puts("usage: gpio N [0|1]\r\n");
            return;
        }
        esp32::gpio::set_output(static_cast<unsigned>(pin));
        esp32::gpio::write(static_cast<unsigned>(pin), value != 0);
        puts("ok\r\n");
        return;
    }

    if (streq(line, "spi-test")) {
        esp32::spi::Bus bus{18, 19, 5, 23};
        esp32::spi::init(bus);
        (void)esp32::spi::transfer(bus, 0xA5);
        puts("spi: transfer complete\r\n");
        return;
    }

    if (streq(line, "i2c-stop")) {
        esp32::i2c::Bus bus{21, 22};
        esp32::i2c::init(bus);
        esp32::i2c::stop(bus);
        puts("i2c: stop generated\r\n");
        return;
    }

    if (*line != 0)
        puts("unknown command; type 'help'\r\n");
}

} // namespace

namespace esp32 {
using namespace esp32_arch;

void kernel_console()
{
    puts("BlockOS> ");
    for (;;) {
        const int c = uart::read_byte();
        if (c < 0) {
            timer_poll();
            cpu_relax();
            continue;
        }

        if (c == '\r' || c == '\n') {
            puts("\r\n");
            g_line[g_line_len] = 0;
            execute(g_line);
            g_line_len = 0;
            puts("BlockOS> ");
            continue;
        }

        if (c == '\b' || c == 0x7f) {
            if (g_line_len != 0) {
                --g_line_len;
                puts("\b \b");
            }
            continue;
        }

        if (c >= 32 && c <= 126 && g_line_len + 1 < sizeof(g_line)) {
            g_line[g_line_len++] = static_cast<char>(c);
            uart::putc(static_cast<char>(c));
        }
    }
}
}

extern "C" void blockos_kernel_main()
{
    esp32::kernel_console();
}
