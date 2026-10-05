#pragma once
#include <stdint.h>
#include <stddef.h>

namespace virtio_gpu {

enum class Status : int {
    Ok = 0,
    InvalidArgument = -1,
    NotFound = -2,
    NotSupported = -3,
    HardwareError = -4,
    Timeout = -5,
};

struct DisplayInfo {
    uint32_t width;
    uint32_t height;
    uint32_t scanout;
    bool enabled;
};

bool init();
bool available();
Status get_display_info(DisplayInfo& out);
Status set_scanout(uint32_t scanout, uint32_t width, uint32_t height);
Status flush(uint32_t scanout, const void* framebuffer, size_t bytes);

} // namespace virtio_gpu
