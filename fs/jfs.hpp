#pragma once

#include <stdint.h>
#include <stddef.h>

namespace jfs {

static constexpr uint32_t JFS_MAGIC = 0x3153464a; // "JFS1" little-endian
static constexpr uint32_t JFS_VERSION = 2;
static constexpr uint32_t JFS_MIN_BLOCK_SIZE = 512;
static constexpr uint32_t JFS_MAX_BLOCK_SIZE = 4096;
static constexpr uint32_t JFS_INODE_SIZE = 512;
static constexpr uint32_t JFS_ROOT_INO = 2;

struct Pxd {
    uint32_t len_addr;
    uint32_t addr2;
};

struct JfsSuperblock {
    char magic[4];
    uint32_t version;
    uint64_t size;
    uint32_t block_size;
    uint16_t log2_block_size;
    uint16_t log2_block_factor;
    uint32_t physical_block_size;
    uint16_t log2_physical_block_size;
    uint16_t reserved0;
    uint32_t ag_size;
    uint32_t flags;
    uint32_t state;
    uint32_t compression;
    Pxd ait2;
    Pxd aim2;
    uint32_t log_device;
    uint32_t log_serial;
    Pxd log_pxd;
    Pxd fsck_pxd;
    uint32_t time_sec;
    uint32_t time_nsec;
    uint32_t fsck_log_length;
    int8_t fsck_log;
    char volume_name[11];
    uint64_t extend_size;
    Pxd extend_fsck;
    Pxd extend_log;
    uint8_t uuid[16];
    char label[16];
    uint8_t log_uuid[16];
};

struct JfsInode {
    uint32_t inode_stamp;
    uint32_t fileset;
    uint32_t number;
    uint32_t generation;
    Pxd inode_extent;
    uint64_t size;
    uint64_t blocks;
    uint32_t links;
    uint32_t uid;
    uint32_t gid;
    uint32_t mode;
    uint32_t atime_sec, atime_nsec;
    uint32_t ctime_sec, ctime_nsec;
    uint32_t mtime_sec, mtime_nsec;
    uint32_t otime_sec, otime_nsec;
    uint8_t acl[16];
    uint8_t ea[16];
    uint32_t next_index;
    uint32_t acl_type;
    uint8_t extension[384];
};

struct Extent {
    uint8_t flags;
    uint64_t file_block;
    uint64_t disk_block;
    uint32_t length;
};

class Reader {
public:
    Reader();

    bool initialize(uint32_t disk_id = 0);
    bool mount();
    bool is_mounted() const;

    uint32_t block_size() const;
    uint64_t block_count() const;
    const JfsSuperblock& superblock() const;

    bool read_block(uint64_t block, uint8_t* buffer);
    bool read_blocks(uint64_t block, uint32_t count, uint8_t* buffer);

    bool read_aggregate_inode(uint32_t inode_number, JfsInode* out);
    bool read_inode_extent(const Pxd& pxd, uint8_t* out, size_t bytes);

    bool read_inode_bytes(const JfsInode& inode,
                          uint64_t offset,
                          uint8_t* out,
                          size_t bytes);

    bool read_file(uint32_t inode_number,
                   uint8_t* out,
                   size_t max_size,
                   size_t* bytes_read);

    bool read_file_range(uint32_t inode_number,
                         uint64_t offset,
                         uint8_t* out,
                         size_t bytes);

    bool validate_superblock() const;
    bool is_clean() const;
    bool has_inline_log() const;
    bool has_compression() const;

    uint64_t pxd_address(const Pxd& pxd) const;
    uint32_t pxd_length(const Pxd& pxd) const;

private:
    uint32_t disk_id_;
    bool initialized_;
    bool mounted_;
    JfsSuperblock sb_;

    bool read_bytes(uint64_t byte_offset, uint8_t* buffer, size_t bytes);
    bool read_superblock_copy(uint64_t byte_offset, JfsSuperblock* out);
    bool read_inode_from_ait(uint32_t inode_number, JfsInode* out);
    bool read_extent_data(const uint8_t* extent_root,
                          uint64_t file_block,
                          uint8_t* out_block);
    bool find_extent(const uint8_t* root,
                     uint64_t file_block,
                     Extent* out);
    bool read_xtree_page(uint64_t block, uint8_t* page);
    bool read_inode_data_block(const JfsInode& inode,
                               uint64_t file_block,
                               uint8_t* out_block);
};

extern Reader filesystem;

bool initialize(uint32_t disk_id = 0);
bool mount();
bool is_mounted();
bool read_file(uint32_t inode_number, uint8_t* out, size_t max_size, size_t* bytes_read);
bool read_file_range(uint32_t inode_number, uint64_t offset, uint8_t* out, size_t bytes);

} // namespace jfs
