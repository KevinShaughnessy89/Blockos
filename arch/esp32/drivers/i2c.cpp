#include "i2c.hpp"
#include "../esp32.hpp"
#include "gpio.hpp"

namespace {

void release_line(unsigned pin)
{
    esp32::gpio::set_input(pin);
}

void pull_low(unsigned pin)
{
    esp32::gpio::set_output(pin);
    esp32::gpio::write(pin, false);
}

void half_clock()
{
    esp32_arch::delay_us(5);
}

void set_sda(const esp32::i2c::Bus& bus, bool high)
{
    if (high) release_line(bus.sda);
    else pull_low(bus.sda);
    half_clock();
}

void set_scl(const esp32::i2c::Bus& bus, bool high)
{
    if (high) release_line(bus.scl);
    else pull_low(bus.scl);
    half_clock();
}

bool read_sda(const esp32::i2c::Bus& bus)
{
    return esp32::gpio::read(bus.sda);
}

} // namespace

namespace esp32::i2c {

void init(const Bus& bus)
{
    release_line(bus.sda);
    release_line(bus.scl);
}

void start(const Bus& bus)
{
    set_sda(bus, true);
    set_scl(bus, true);
    set_sda(bus, false);
    set_scl(bus, false);
}

void stop(const Bus& bus)
{
    set_sda(bus, false);
    set_scl(bus, true);
    set_sda(bus, true);
}

bool write_byte(const Bus& bus, uint8_t value)
{
    for (int bit = 7; bit >= 0; --bit) {
        set_sda(bus, (value & (1u << bit)) != 0);
        set_scl(bus, true);
        set_scl(bus, false);
    }

    set_sda(bus, true);
    set_scl(bus, true);
    const bool ack = !read_sda(bus);
    set_scl(bus, false);
    return ack;
}

uint8_t read_byte(const Bus& bus, bool ack)
{
    uint8_t value = 0;
    set_sda(bus, true);
    for (int bit = 7; bit >= 0; --bit) {
        set_scl(bus, true);
        value = static_cast<uint8_t>((value << 1) | (read_sda(bus) ? 1u : 0u));
        set_scl(bus, false);
    }

    set_sda(bus, !ack);
    set_scl(bus, true);
    set_scl(bus, false);
    set_sda(bus, true);
    return value;
}

bool write(const Bus& bus, uint8_t address7, const uint8_t* data, size_t length)
{
    start(bus);
    if (!write_byte(bus, static_cast<uint8_t>(address7 << 1))) {
        stop(bus);
        return false;
    }

    for (size_t i = 0; i < length; ++i) {
        if (!write_byte(bus, data[i])) {
            stop(bus);
            return false;
        }
    }

    stop(bus);
    return true;
}

} // namespace esp32::i2c
