#pragma once
#include <stdint.h>
#include <stddef.h>

namespace blockos::swap {

typedef bool (*BlockReadFn)(void* ctx, uint64_t block, void* out, size_t bytes);
typedef bool (*BlockWriteFn)(void* ctx, uint64_t block, const void* data, size_t bytes);

struct Device {
    void* ctx;
    BlockReadFn read;
    BlockWriteFn write;
    uint64_t start_block;
    uint32_t block_size;
    uint32_t slots;
};

bool init(const Device& dev);
bool enabled();
int64_t allocate_slot();
bool free_slot(uint32_t slot);
bool read_slot(uint32_t slot, void* out, size_t bytes);
bool write_slot(uint32_t slot, const void* data, size_t bytes);
uint32_t slot_count();

}
