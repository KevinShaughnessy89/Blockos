#pragma once
#include <stdint.h>
#include <stddef.h>

namespace ufs {

enum class Version : uint8_t { NONE=0, UFS1=1, UFS2=2 };

struct InodeInfo {
    uint64_t inode;
    uint16_t mode;
    uint64_t size;
    uint64_t blocks;
    uint32_t uid;
    uint32_t gid;
    uint64_t atime;
    uint64_t mtime;
    uint64_t ctime;
    bool directory;
};

bool init(int disk_id = 0);
bool is_initialized();
Version version();
uint32_t block_size();
uint64_t total_blocks();

bool read_bytes(uint64_t offset, uint8_t* out, size_t size);
bool read_inode(uint64_t inode_number, InodeInfo* out);
bool read_inode_data(uint64_t inode_number, uint64_t offset, uint8_t* out, size_t size);
bool read_file(const char* path, uint8_t* out, size_t max_size, size_t* out_size);
bool find_inode(const char* path, uint64_t* inode_number);
bool directory_entry(uint64_t dir_inode, size_t index, char* name, size_t name_size,
                     uint64_t* inode_number, bool* is_directory);

}
