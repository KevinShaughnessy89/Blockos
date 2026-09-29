#pragma once

#include <cstddef>
#include <cstdint>

namespace nvme {

bool init();
bool is_ready();
uint64_t capacity_sectors();
uint32_t sector_size();

bool read_sector(uint64_t lba, void* buffer);
bool write_sector(uint64_t lba, const void* buffer);
bool read_sectors(uint64_t lba, uint32_t count, void* buffer);
bool write_sectors(uint64_t lba, uint32_t count, const void* buffer);
bool flush();

uint32_t controller_status();
uint32_t controller_error_count();

} // namespace nvme
