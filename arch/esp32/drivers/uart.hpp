#pragma once

#include <stdint.h>

namespace esp32::uart {

bool init(unsigned port = 0, uint32_t baud = 115200);
void putc(char c);
void puts(const char* text);
int read_byte();
bool rx_ready();

} // namespace esp32::uart
