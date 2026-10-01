#pragma once

namespace esp32::gpio {

void reset_all();
bool valid(unsigned pin);
void set_input(unsigned pin);
void set_output(unsigned pin);
bool read(unsigned pin);
void write(unsigned pin, bool level);

} // namespace esp32::gpio
