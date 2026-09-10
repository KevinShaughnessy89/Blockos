#pragma once

#include <stdint.h>
#include <stddef.h>

namespace iso9660
{

/*
 * ISO 9660 file flags.
 */
static constexpr uint8_t FILE_FLAG_HIDDEN       = 0x01;
static constexpr uint8_t FILE_FLAG_DIRECTORY    = 0x02;
static constexpr uint8_t FILE_FLAG_ASSOCIATED   = 0x04;
static constexpr uint8_t FILE_FLAG_RECORD       = 0x08;
static constexpr uint8_t FILE_FLAG_PROTECTION   = 0x10;
static constexpr uint8_t FILE_FLAG_MULTI_EXTENT = 0x80;

/*
 * Public directory entry returned by the reader.
 *
 * name is UTF-8 when Joliet is used. For normal ISO 9660 it is the
 * normalized ISO name (the ";1" version suffix is removed).
 */
struct ISO9660DirectoryEntry
{
    char name[256];

    uint64_t extent_lba;
    uint64_t size;

    uint8_t flags;
    uint8_t file_unit_size;
    uint8_t interleave_gap_size;

    uint16_t volume_sequence;

    bool directory;
    bool hidden;
    bool associated;
    bool multi_extent;
    bool rock_ridge;
};

/*
 * Read-only ISO 9660 filesystem reader.
 *
 * Block device access is provided by BlockOS's fs/vfs_blk_adapter.*
 * and is therefore based on the existing VirtIO block backend.
 *
 * Supported:
 *   - ISO 9660 Primary Volume Descriptor
 *   - ISO 9660 directory records
 *   - root directory traversal
 *   - arbitrary nested path lookup
 *   - file reads
 *   - directory listing
 *   - Joliet supplementary volume descriptor / UCS-2BE names
 *   - Rock Ridge NM (alternate filename) recognition
 *
 * This implementation is intentionally read-only. ISO 9660 images are
 * normally treated as immutable filesystem images by BlockOS.
 */
class ISO9660Reader
{
private:
    bool mounted_;
    bool joliet_;
    bool rock_ridge_;

    uint32_t logical_block_size_;
    uint64_t volume_space_size_;

    uint32_t root_extent_lba_;
    uint32_t root_data_length_;
    uint8_t root_flags_;

    char volume_identifier_[33];

    /* Last-selected volume descriptor type: 1 = PVD, 2 = Joliet SVD. */
    uint8_t descriptor_type_;

    bool read_logical_block(uint64_t lba, uint8_t* buffer) const;

    bool read_volume_descriptor(
        uint64_t descriptor_lba,
        uint8_t* buffer);

    bool parse_descriptor(
        const uint8_t* buffer,
        uint8_t type,
        bool& valid,
        bool& joliet_candidate);

    bool parse_root_record(
        const uint8_t* record,
        size_t available,
        uint32_t& extent_lba,
        uint32_t& data_length,
        uint8_t& flags) const;

    bool parse_directory_record(
        const uint8_t* record,
        size_t available,
        ISO9660DirectoryEntry& out) const;

    bool decode_iso_name(
        const uint8_t* source,
        size_t source_length,
        char* destination,
        size_t destination_capacity) const;

    bool decode_joliet_name(
        const uint8_t* source,
        size_t source_length,
        char* destination,
        size_t destination_capacity) const;

    bool parse_rock_ridge_name(
        const uint8_t* system_use,
        size_t system_use_length,
        char* destination,
        size_t destination_capacity) const;

    bool scan_rock_ridge_root();

    bool string_equals_component(
        const char* requested,
        const char* stored) const;

    bool find_in_directory(
        uint64_t directory_lba,
        uint64_t directory_size,
        const char* component,
        ISO9660DirectoryEntry& out) const;

    bool resolve_path(
        const char* path,
        ISO9660DirectoryEntry& out) const;

    bool resolve_parent_directory(
        const char* path,
        uint64_t& parent_lba,
        uint64_t& parent_size,
        char* leaf,
        size_t leaf_capacity) const;

    bool read_extent(
        uint64_t extent_lba,
        uint64_t file_size,
        uint8_t* destination,
        uint64_t destination_capacity,
        uint64_t& bytes_read) const;

public:
    ISO9660Reader();

    /* Mount/initialize the currently available BlockOS block backend. */
    bool initialize();
    bool mount();
    bool unmount();

    bool ready() const;
    bool is_iso9660() const;
    bool is_joliet() const;
    bool has_rock_ridge() const;

    uint32_t logical_block_size() const;
    uint64_t volume_space_size() const;
    const char* volume_identifier() const;

    uint64_t root_extent_lba() const;
    uint64_t root_directory_size() const;

    bool get_entry(
        const char* path,
        ISO9660DirectoryEntry& out) const;

    bool read_file(
        const char* path,
        uint8_t* destination,
        size_t max_size,
        size_t* bytes_read = nullptr) const;

    bool list_directory(
        const char* path,
        ISO9660DirectoryEntry* entries,
        size_t capacity,
        size_t* count = nullptr) const;
};

extern ISO9660Reader iso9660_fs;

/* Convenience API used by future mount/VFS integration. */
bool iso9660_init();
bool iso9660_read_file(
    const char* path,
    uint8_t* destination,
    size_t max_size,
    size_t* bytes_read = nullptr);

} // namespace iso9660
