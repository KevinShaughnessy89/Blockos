#pragma once
#include <stdint.h>

namespace uefi_runtime {

struct Services {
    uintptr_t get_time;
    uintptr_t set_time;
    uintptr_t get_variable;
    uintptr_t set_variable;
    uintptr_t reset_system;
};

bool init(const Services& services);
bool available();
const Services& services();

} // namespace uefi_runtime
