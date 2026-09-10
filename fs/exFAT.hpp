#pragma once

#include <cstddef>
#include <cstdint>

namespace exfat {

constexpr uint32_t EXFAT_ENTRY_SIZE = 32;
constexpr uint8_t ENTRY_END = 0x00;
constexpr uint8_t ENTRY_BITMAP = 0x81;
constexpr uint8_t ENTRY_UPCASE = 0x82;
constexpr uint8_t ENTRY_FILE = 0x85;
constexpr uint8_t ENTRY_STREAM = 0xC0;
constexpr uint8_t ENTRY_NAME = 0xC1;
constexpr uint8_t ENTRY_VOLUME_LABEL = 0x83;
constexpr uint8_t ENTRY_GUID = 0xA0;

struct FileInfo {
    char name[260];
    uint32_t first_cluster;
    uint64_t size;
    uint16_t attributes;
    uint8_t name_length;
    bool directory;
    bool no_fat_chain;
};

struct VolumeInfo {
    uint64_t partition_offset;
    uint64_t volume_length;
    uint32_t fat_offset;
    uint32_t fat_length;
    uint32_t cluster_heap_offset;
    uint32_t cluster_count;
    uint32_t root_cluster;
    uint32_t volume_serial;
    uint16_t revision;
    uint16_t volume_flags;
    uint32_t bytes_per_sector;
    uint32_t sectors_per_cluster;
    uint32_t cluster_size;
    uint8_t fat_count;
};

class ExFatReader {
private:
    VolumeInfo volume_;
    bool ready_;
    uint8_t* cluster_buffer_;
    size_t cluster_buffer_size_;

    bool read_sector(uint64_t sector, uint8_t* buffer);
    bool read_sectors(uint64_t sector, uint32_t count, uint8_t* buffer);
    bool read_bytes(uint64_t offset, uint32_t size, uint8_t* buffer);
    bool read_cluster(uint32_t cluster, uint8_t* buffer);
    bool read_fat_entry(uint32_t cluster, uint32_t* next);
    bool cluster_valid(uint32_t cluster) const;
    uint64_t cluster_sector(uint32_t cluster) const;
    uint64_t data_offset(uint32_t cluster, uint64_t offset) const;

    bool validate_boot_sector(const uint8_t* boot) const;
    bool load_boot_sector();
    bool find_in_directory(uint32_t directory_cluster, const char* wanted,
                           FileInfo* out);
    bool find_path(const char* path, FileInfo* out);
    bool read_directory_entries(uint32_t directory_cluster,
                                FileInfo* entries, size_t capacity,
                                size_t* count);
    bool decode_file_entry(const uint8_t* entry, const uint8_t* directory,
                           uint32_t directory_offset, FileInfo* out);
    bool read_file_data(const FileInfo& file, uint8_t* dest,
                        size_t max_size, size_t* bytes_read);
    bool compare_name(const char* a, const char* b) const;
    void decode_utf16_name(const uint8_t* entries, uint8_t name_length,
                           char* out, size_t out_size) const;
    uint16_t fold_utf16(uint16_t c) const;
    void copy_string(char* dst, size_t dst_size, const char* src) const;
    bool next_cluster(uint32_t current, uint32_t* next) const;

public:
    ExFatReader();
    ~ExFatReader();

    bool initialize();
    bool mount();
    void unmount();
    bool ready() const;
    bool is_exfat() const;

    const VolumeInfo& volume_info() const;

    bool find(const char* path, FileInfo* out);
    bool stat(const char* path, FileInfo* out);
    bool read_file(const char* path, uint8_t* dest, size_t max_size,
                   size_t* bytes_read = nullptr);
    bool list_directory(const char* path, FileInfo* entries,
                        size_t capacity, size_t* count);

    // exFAT support is intentionally read-only in this first driver.
    bool write_file(const char* path, const uint8_t* data, size_t size);
};

extern ExFatReader exfat_fs;

bool exfat_init();
bool exfat_read_file(const char* path, uint8_t* dest, size_t max_size,
                    size_t* bytes_read = nullptr);

} // namespace exfat
