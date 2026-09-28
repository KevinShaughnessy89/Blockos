#pragma once
#include <cstddef>

namespace dma {
    void* alloc(size_t size, size_t align = 4096);
    void free(void* ptr);
}
