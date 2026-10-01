#pragma once

#include <stddef.h>
#include <stdint.h>

namespace esp32::i2c {

struct Bus {
    unsigned sda;
    unsigned scl;
};

void init(const Bus& bus);
void start(const Bus& bus);
void stop(const Bus& bus);
bool write_byte(const Bus& bus, uint8_t value);
uint8_t read_byte(const Bus& bus, bool ack);
bool write(const Bus& bus, uint8_t address7, const uint8_t* data, size_t length);

} // namespace esp32::i2c
