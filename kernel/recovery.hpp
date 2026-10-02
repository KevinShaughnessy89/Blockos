#pragma once
#include <stdint.h>
#include <stddef.h>

namespace blockos::recovery {

enum class BootStage : uint8_t { Cold=0, Bootloader, Kernel, Userspace, Desktop, Ready, Failed };

typedef bool (*PersistFn)(void* ctx, uint32_t attempts, BootStage stage, uint32_t crc);

void configure(PersistFn load, PersistFn store, void* ctx);
void begin_boot();
void mark_stage(BootStage stage);
void mark_failed(uint32_t reason);
void mark_ready();
bool should_enter_recovery();
uint32_t boot_attempts();
uint32_t last_failure();
BootStage stage();
uint32_t crc32(const void* data, size_t len, uint32_t seed = 0xFFFFFFFFu);
void clear_failure();

}
