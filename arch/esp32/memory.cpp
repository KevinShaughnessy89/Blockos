#include "esp32.hpp"

extern "C" {
extern uint8_t _bss_start;
extern uint8_t _bss_end;
extern uint8_t _heap_start;
extern uint8_t _heap_end;
extern uint8_t _stack_top;
}

namespace esp32_arch {

void memory_init()
{
    uint8_t* begin = &_bss_start;
    uint8_t* end = &_bss_end;

    while (begin < end) {
        *begin++ = 0;
    }
}

uintptr_t ram_begin()
{
    return reinterpret_cast<uintptr_t>(&_heap_start);
}

uintptr_t ram_end()
{
    return reinterpret_cast<uintptr_t>(&_heap_end);
}

uintptr_t stack_top()
{
    return reinterpret_cast<uintptr_t>(&_stack_top);
}

} // namespace esp32_arch
