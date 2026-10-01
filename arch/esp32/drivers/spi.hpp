#pragma once

#include <stdint.h>

namespace esp32::spi {

struct Bus {
    unsigned sck;
    unsigned miso;
    unsigned cs;
    unsigned mosi;
};

void init(const Bus& bus);
uint8_t transfer(const Bus& bus, uint8_t value);
void select(const Bus& bus);
void deselect(const Bus& bus);

} // namespace esp32::spi
