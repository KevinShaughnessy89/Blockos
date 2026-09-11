#pragma once

#include <stdint.h>
#include <stddef.h>

/* BlockOS EXT3 reader.
 *
 * EXT3 is EXT2 + the JBD journal.  This implementation deliberately keeps
 * the public interface small and uses only the BlockOS VirtIO block API.
 * It is a read-only filesystem reader; journal contents are inspected and
 * exposed through journal_* accessors, but are never modified.
 */

struct Ext3Superblock {
    uint32_t inodes_count;
    uint32_t blocks_count_lo;
    uint32_t r_blocks_count_lo;
    uint32_t free_blocks_count_lo;
    uint32_t free_inodes_count;
    uint32_t first_data_block;
    uint32_t log_block_size;
    uint32_t log_frag_size;
    uint32_t blocks_per_group;
    uint32_t frags_per_group;
    uint32_t inodes_per_group;
    uint32_t mtime;
    uint32_t wtime;
    uint16_t mnt_count;
    uint16_t max_mnt_count;
    uint16_t magic;
    uint16_t state;
    uint16_t errors;
    uint16_t minor_rev_level;
    uint32_t lastcheck;
    uint32_t checkinterval;
    uint32_t creator_os;
    uint32_t rev_level;
    uint16_t def_resuid;
    uint16_t def_resgid;
    uint32_t first_ino;
    uint16_t inode_size;
    uint16_t block_group_nr;
    uint32_t feature_compat;
    uint32_t feature_incompat;
    uint32_t feature_ro_compat;
    uint8_t reserved[204];
} __attribute__((packed));

struct Ext3Inode {
    uint16_t mode;
    uint16_t uid;
    uint32_t size_lo;
    uint32_t atime;
    uint32_t ctime;
    uint32_t mtime;
    uint32_t dtime;
    uint16_t gid;
    uint16_t links_count;
    uint32_t blocks;
    uint32_t flags;
    uint32_t osd1;
    uint8_t block[60];
    uint32_t generation;
    uint32_t file_acl;
    uint32_t size_high_or_dir_acl;
    uint32_t faddr;
    uint8_t osd2[12];
} __attribute__((packed));

struct Ext3DirectoryEntry {
    uint32_t inode;
    uint16_t rec_len;
    uint8_t name_len;
    uint8_t file_type;
} __attribute__((packed));

class Ext3Reader {
public:
    Ext3Reader();

    bool mount();

    bool is_mounted() const;
    uint32_t get_block_size() const;
    uint32_t get_inode_size() const;
    uint32_t get_inode_count() const;
    uint32_t get_block_count() const;

    bool read_inode(uint32_t inode_num, Ext3Inode* out);
    uint32_t find_file_inode(const char* path);
    bool load_file_by_inode(uint32_t inode_num, uint8_t* dest, size_t max_size);
    bool read_directory(uint32_t inode_num, uint32_t index,
                        char* name, size_t name_size,
                        uint32_t* child_inode, uint8_t* file_type);

    /* EXT3/JBD information. */
    bool has_journal() const;
    uint32_t journal_inode() const;
    uint32_t journal_block_size() const;
    uint32_t journal_first_block() const;
    uint32_t journal_maxlen() const;
    uint32_t journal_sequence() const;

private:
    Ext3Superblock sb;
    uint32_t block_size;
    uint32_t inode_size;
    uint64_t group_desc_block;
    uint32_t group_desc_size;
    bool mounted;

    bool journal_present;
    uint32_t journal_ino;
    uint32_t journal_bsize;
    uint32_t journal_first;
    uint32_t journal_maxlen_value;
    uint32_t journal_sequence_value;

    bool read_bytes(uint64_t offset, void* buffer, size_t size);
    bool read_block(uint64_t block, uint8_t* buffer);
    bool read_group_desc(uint32_t group, uint8_t* out, size_t out_size);
    bool read_inode_raw(uint32_t inode_num, uint8_t* out, size_t out_size);

    bool get_data_block(const Ext3Inode& inode, uint64_t logical, uint64_t* physical);
    bool get_indirect_block(uint64_t block, uint32_t depth,
                            uint64_t index, uint64_t* physical);
    bool get_extent_block(const uint8_t* root, uint64_t logical,
                          uint64_t* physical);

    bool find_in_directory(uint32_t dir_inode, const char* component,
                           uint32_t* child_inode);
    bool path_next_component(const char* path, size_t* pos,
                             char* component, size_t component_size);

    bool parse_journal(const Ext3Inode& journal_inode_data);
};

extern Ext3Reader ext3_filesystem;
