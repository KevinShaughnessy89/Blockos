#include "gpio.hpp"
#include "mmio.hpp"

namespace {

constexpr uintptr_t GPIO_BASE = 0x3FF44000u;
constexpr uintptr_t OUT = 0x04;
constexpr uintptr_t OUT_W1TS = 0x08;
constexpr uintptr_t OUT_W1TC = 0x0C;
constexpr uintptr_t OUT1 = 0x10;
constexpr uintptr_t OUT1_W1TS = 0x14;
constexpr uintptr_t OUT1_W1TC = 0x18;
constexpr uintptr_t ENABLE = 0x20;
constexpr uintptr_t ENABLE_W1TS = 0x24;
constexpr uintptr_t ENABLE_W1TC = 0x28;
constexpr uintptr_t ENABLE1 = 0x30;
constexpr uintptr_t ENABLE1_W1TS = 0x34;
constexpr uintptr_t ENABLE1_W1TC = 0x38;
constexpr uintptr_t IN = 0x3C;
constexpr uintptr_t IN1 = 0x40;

inline bool high_bank(unsigned pin) { return pin >= 32; }
inline uint32_t bit(unsigned pin) { return 1u << (pin & 31u); }

} // namespace

namespace esp32::gpio {

bool valid(unsigned pin) { return pin < 40; }

void reset_all()
{
    mmio::write(GPIO_BASE + OUT_W1TC, 0xFFFFFFFFu);
    mmio::write(GPIO_BASE + OUT1_W1TC, 0x000000FFu);
}

void set_input(unsigned pin)
{
    if (!valid(pin)) return;
    if (high_bank(pin))
        mmio::write(GPIO_BASE + ENABLE1_W1TC, bit(pin));
    else
        mmio::write(GPIO_BASE + ENABLE_W1TC, bit(pin));
}

void set_output(unsigned pin)
{
    if (!valid(pin)) return;
    if (high_bank(pin))
        mmio::write(GPIO_BASE + ENABLE1_W1TS, bit(pin));
    else
        mmio::write(GPIO_BASE + ENABLE_W1TS, bit(pin));
}

bool read(unsigned pin)
{
    if (!valid(pin)) return false;
    const uint32_t value = high_bank(pin)
        ? mmio::read(GPIO_BASE + IN1)
        : mmio::read(GPIO_BASE + IN);
    return (value & bit(pin)) != 0;
}

void write(unsigned pin, bool level)
{
    if (!valid(pin)) return;
    if (high_bank(pin)) {
        mmio::write(GPIO_BASE + (level ? OUT1_W1TS : OUT1_W1TC), bit(pin));
    } else {
        mmio::write(GPIO_BASE + (level ? OUT_W1TS : OUT_W1TC), bit(pin));
    }
}

} // namespace esp32::gpio
