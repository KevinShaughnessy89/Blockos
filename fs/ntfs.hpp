#pragma once

#include <cstddef>
#include <cstdint>

namespace ntfs {

struct FileInfo {
    uint64_t mft_record;
    uint64_t size;
    bool directory;
    char name[256];
};

bool init();
bool probe();
bool mounted();

uint32_t bytes_per_sector();
uint32_t bytes_per_cluster();
uint32_t file_record_size();
uint64_t total_sectors();
uint64_t mft_record();

bool find(const char* path, FileInfo* out);
bool read_file(const char* path, uint64_t offset, void* buffer, uint32_t size,
               uint32_t* out_read = nullptr);

bool list_directory(const char* path,
                    bool (*callback)(const FileInfo& info, void* user),
                    void* user = nullptr);

} // namespace ntfs
