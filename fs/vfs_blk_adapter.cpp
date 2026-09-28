#include "vfs_blk_adapter.hpp"

#include <stdint.h>
#include <stddef.h>

/*
 * Controller-neutral storage ABI.  These symbols are implemented by the
 * existing drivers/device_manager.cpp storage registry; no new HAL tree is
 * required.  Flash-capable controllers register erase geometry and their own
 * physical wear-leveling/FTL information through that registry.
 */
extern "C" {
bool blockos_storage_init();
const char* blockos_storage_backend_name();
uint64_t blockos_storage_capacity_sectors();
uint32_t blockos_storage_sector_size();
bool blockos_storage_is_flash();
bool blockos_storage_has_native_wear_leveling();
uint32_t blockos_storage_erase_block_sectors();
bool blockos_storage_supports_erase();
bool blockos_storage_read(uint64_t, uint32_t, void*);
bool blockos_storage_write(uint64_t, uint32_t, const void*);
bool blockos_storage_flush();
bool blockos_storage_erase(uint64_t, uint32_t);
}

namespace vfs_blk_adapter {

static bool g_initialized = false;
static uint32_t g_sector_size = 512;
static uint64_t g_capacity = 0;
static bool g_flash = false;
static bool g_native_wear_leveling = false;
static bool g_erase_supported = false;
static uint32_t g_erase_block_sectors = 0;

bool init_backend()
{
    if (g_initialized)
        return true;

    if (!blockos_storage_init())
        return false;

    const uint32_t sector_size = blockos_storage_sector_size();
    const uint64_t capacity = blockos_storage_capacity_sectors();
    if (sector_size == 0 || capacity == 0)
        return false;

    g_sector_size = sector_size;
    g_capacity = capacity;
    g_flash = blockos_storage_is_flash();
    g_native_wear_leveling = blockos_storage_has_native_wear_leveling();
    g_erase_supported = blockos_storage_supports_erase();
    g_erase_block_sectors = blockos_storage_erase_block_sectors();
    g_initialized = true;
    return true;
}

bool read_sector_to_vfs(uint64_t sector, uint8_t* buf)
{
    if (!buf || !init_backend() || g_sector_size != 512)
        return false;
    return blockos_storage_read(sector, 1, buf);
}

bool write_sector_from_vfs(uint64_t sector, const uint8_t* buf)
{
    if (!buf || !init_backend() || g_sector_size != 512)
        return false;
    if (!blockos_storage_write(sector, 1, buf))
        return false;
    return blockos_storage_flush();
}

bool read_sectors_to_vfs(uint64_t sector, uint32_t count, uint8_t* buf)
{
    if (!buf || count == 0 || !init_backend() || g_sector_size != 512)
        return false;
    return blockos_storage_read(sector, count, buf);
}

bool write_sectors_from_vfs(uint64_t sector, uint32_t count, const uint8_t* buf)
{
    if (!buf || count == 0 || !init_backend() || g_sector_size != 512)
        return false;
    if (!blockos_storage_write(sector, count, buf))
        return false;
    return blockos_storage_flush();
}

bool flush_backend()
{
    return init_backend() && blockos_storage_flush();
}

/*
 * These helpers intentionally expose the physical-media capabilities without
 * inventing a vendor-specific NAND/eMMC implementation.  A raw NAND/SDHCI/
 * eMMC controller implemented in an existing .cpp can register itself with
 * blockos_storage_register_flash_backend().  When that backend advertises an
 * erase geometry, the controller-neutral FTL layer has the exact information
 * it needs to perform persistent remapping/erase policy.
 */
extern "C" bool blockos_vfs_storage_is_flash()
{
    return init_backend() && g_flash;
}

extern "C" bool blockos_vfs_storage_has_native_wear_leveling()
{
    return init_backend() && g_native_wear_leveling;
}

extern "C" bool blockos_vfs_storage_supports_erase()
{
    return init_backend() && g_erase_supported;
}

extern "C" uint32_t blockos_vfs_storage_erase_block_sectors()
{
    return init_backend() ? g_erase_block_sectors : 0;
}

extern "C" uint64_t blockos_vfs_storage_capacity()
{
    return init_backend() ? g_capacity : 0;
}

extern "C" const char* blockos_vfs_storage_backend_name()
{
    return init_backend() ? blockos_storage_backend_name() : nullptr;
}

extern "C" bool blockos_vfs_storage_erase(uint64_t sector, uint32_t count)
{
    return init_backend() && g_erase_supported &&
           blockos_storage_erase(sector, count);
}

} // namespace vfs_blk_adapter
