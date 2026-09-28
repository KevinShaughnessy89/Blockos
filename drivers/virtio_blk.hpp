#pragma once

#include <stdint.h>
#include <stddef.h>

namespace virtio_blk
{
    bool init();

    bool is_ready();

    uint64_t capacity_sectors();

    bool flush();

    bool read_sector(
        uint64_t sector,
        uint8_t* out_buf
    );

    bool write_sector(
        uint64_t sector,
        const uint8_t* in_buf
    );

    bool read_sectors(
        uint64_t sector,
        uint32_t count,
        uint8_t* out_buf
    );

    bool write_sectors(
        uint64_t sector,
        uint32_t count,
        const uint8_t* in_buf
    );
}
