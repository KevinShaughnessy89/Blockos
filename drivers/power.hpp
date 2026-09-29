#pragma once

#include <cstdint>

extern "C" {
#include <efi.h>
}

namespace power {

enum class State : uint8_t {
    Unknown = 0,
    Ready,
    Sleep3,
    Shutdown5
};

bool init(EFI_SYSTEM_TABLE* system_table);
bool is_available();
State state();

/* ACPI system power transitions. */
bool sleep();
bool shutdown();
void idle();

/* Best-effort ACPI platform information. */
bool battery_available();
bool thermal_available();

} // namespace power
