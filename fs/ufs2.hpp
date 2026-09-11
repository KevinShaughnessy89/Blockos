#pragma once

#include <stdint.h>
#include <stddef.h>

namespace ufs2 {

static constexpr uint32_t UFS2_MAGIC = 0x19540119u;
static constexpr uint32_t UFS2_ROOT_INO = 2u;
static constexpr uint32_t UFS2_INODE_SIZE = 256u;
static constexpr uint32_t UFS2_NDADDR = 12u;
static constexpr uint32_t UFS2_NIADDR = 3u;
static constexpr uint32_t UFS2_MAX_NAME = 255u;

// UFS2 file type bits (BSD/FFS format).
static constexpr uint16_t IFMT  = 0170000u;
static constexpr uint16_t IFDIR = 0040000u;
static constexpr uint16_t IFREG = 0100000u;
static constexpr uint16_t IFLNK = 0120000u;

struct Inode {
    uint16_t mode;
    uint16_t nlink;
    uint32_t uid;
    uint32_t gid;
    uint32_t block_size;
    uint64_t size;
    uint64_t blocks;
    int64_t atime;
    int64_t mtime;
    int64_t ctime;
    int64_t birthtime;
    uint32_t flags;
    uint32_t generation;
    uint64_t direct[UFS2_NDADDR];
    uint64_t indirect[UFS2_NIADDR];
};

struct SuperblockInfo {
    uint64_t location;
    uint64_t fs_size;
    uint64_t fs_dsize;
    uint32_t block_size;
    uint32_t fragment_size;
    uint32_t fragments_per_block;
    uint32_t inodes_per_group;
    uint32_t fragments_per_group;
    uint32_t inode_block;
    uint32_t inode_size;
    uint32_t inodes_per_block;
    uint32_t indirect_entries;
    uint32_t cylinder_groups;
    bool big_endian;
};

bool initialize(uint64_t sector_offset = 0);
bool mount(uint64_t sector_offset = 0);
bool is_initialized();
const SuperblockInfo* superblock();

bool read_inode(uint64_t inode_number, Inode* out);
bool read_file(uint64_t inode_number, uint8_t* dest, size_t max_size,
               size_t* out_size = nullptr);
bool read_file_range(uint64_t inode_number, uint64_t offset,
                     uint8_t* dest, size_t length, size_t* out_size = nullptr);

bool find_inode(const char* path, uint64_t* out_inode);
bool read_path(const char* path, uint8_t* dest, size_t max_size,
               size_t* out_size = nullptr);
bool read_path_range(const char* path, uint64_t offset,
                     uint8_t* dest, size_t length, size_t* out_size = nullptr);

bool is_directory(uint64_t inode_number);
bool is_regular_file(uint64_t inode_number);
bool is_symlink(uint64_t inode_number);

// Returns the symlink target. The output is always NUL terminated.
bool read_symlink(uint64_t inode_number, char* out, size_t out_size);

// Finds one child in a directory. name must be a single path component.
bool lookup_child(uint64_t directory_inode, const char* name,
                  uint64_t* out_inode);

} // namespace ufs2
