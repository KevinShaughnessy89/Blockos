#include "virtio_blk_full.hpp"
#include "virtio_blk.hpp"

bool virtio_blk_full::read_sector(uint64_t sector, uint8_t* out_buf)
{
    return virtio_blk::read_sector(sector, out_buf);
}

bool virtio_blk_full::write_sector(uint64_t sector, const uint8_t* in_buf)
{
    return virtio_blk::write_sector(sector, in_buf);
}

bool virtio_blk_full::read_sectors(uint64_t sector, uint32_t count, uint8_t* out_buf)
{
    return virtio_blk::read_sectors(sector, count, out_buf);
}

bool virtio_blk_full::write_sectors(uint64_t sector, uint32_t count, const uint8_t* in_buf)
{
    return virtio_blk::write_sectors(sector, count, in_buf);
}

bool virtio_blk_full::flush()
{
    return virtio_blk::flush();
}
