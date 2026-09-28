#pragma once

#include <stddef.h>
#include <stdint.h>

extern "C" {
#include <efi.h>
}

namespace uefi_smp {

bool start_aps(EFI_SYSTEM_TABLE* system_table);
void release_aps();
size_t cpu_count();
size_t started_count();
bool active();

}
