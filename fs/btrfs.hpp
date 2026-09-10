#pragma once

#include <cstddef>
#include <cstdint>

namespace btrfs
{
struct FileInfo
{
    char name[256];
    uint64_t inode;
    uint64_t size;
    uint32_t mode;
    bool directory;
    bool regular;
};

using ListCallback = bool (*)(const FileInfo& info, void* context);

bool init();
bool probe();
bool mounted();

bool find(const char* path, FileInfo* out);
bool read_file(const char* path, uint64_t offset, uint8_t* dest,
               size_t size, size_t* out_read = nullptr);
bool list_directory(const char* path, ListCallback callback, void* context);

uint64_t total_bytes();
uint64_t used_bytes();
uint32_t sector_size();
uint32_t node_size();

} // namespace btrfs
