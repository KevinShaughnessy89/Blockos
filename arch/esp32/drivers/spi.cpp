#include "spi.hpp"
#include "../esp32.hpp"
#include "gpio.hpp"

namespace esp32::spi {

static void clock(const Bus& bus, bool level)
{
    gpio::write(bus.sck, level);
    esp32_arch::delay_us(1);
}

void init(const Bus& bus)
{
    gpio::set_output(bus.sck);
    gpio::set_output(bus.cs);
    gpio::set_output(bus.mosi);
    gpio::set_input(bus.miso);
    gpio::write(bus.sck, false);
    gpio::write(bus.cs, true);
    gpio::write(bus.mosi, false);
}

void select(const Bus& bus)
{
    gpio::write(bus.cs, false);
}

void deselect(const Bus& bus)
{
    gpio::write(bus.cs, true);
}

uint8_t transfer(const Bus& bus, uint8_t value)
{
    uint8_t result = 0;
    for (int bit = 7; bit >= 0; --bit) {
        gpio::write(bus.mosi, (value & (1u << bit)) != 0);
        clock(bus, true);
        result = static_cast<uint8_t>((result << 1) | (gpio::read(bus.miso) ? 1u : 0u));
        clock(bus, false);
    }
    return result;
}

} // namespace esp32::spi
