#pragma once

#include <cstddef>
#include <cstdint>

namespace udf {

constexpr uint32_t SECTOR_SIZE = 512;
constexpr uint16_t TAG_AVDP = 2;
constexpr uint16_t TAG_PARTITION_DESCRIPTOR = 5;
constexpr uint16_t TAG_LOGICAL_VOLUME_DESCRIPTOR = 6;
constexpr uint16_t TAG_TERMINATING_DESCRIPTOR = 8;
constexpr uint16_t TAG_FILE_SET_DESCRIPTOR = 0x0100;
constexpr uint16_t TAG_FILE_IDENTIFIER_DESCRIPTOR = 0x0101;
constexpr uint16_t TAG_FILE_ENTRY = 0x0105;
constexpr uint16_t TAG_EXTENDED_FILE_ENTRY = 0x010A;

struct Extent {
    uint32_t location;
    uint32_t partition;
    uint32_t length;
};

struct FileInfo {
    uint32_t icb_location;
    uint32_t partition;
    uint64_t size;
    uint8_t file_type;
    bool directory;
    char name[256];
};

struct DirectoryEntry {
    FileInfo info;
};

class UDFReader {
public:
    UDFReader();

    bool initialize();
    bool mount();
    void unmount();
    bool ready() const;
    bool is_udf() const;

    uint32_t logical_block_size() const;
    uint32_t partition_start() const;
    uint32_t partition_length() const;

    bool find(const char* path, FileInfo* out);
    bool stat(const char* path, FileInfo* out);
    bool read_file(const char* path, uint8_t* dest, size_t max_size,
                   size_t* bytes_read = nullptr);
    bool list_directory(const char* path, DirectoryEntry* entries,
                        size_t capacity, size_t* count);

private:
    struct LongAd {
        uint32_t length;
        uint32_t location;
        uint16_t partition;
    };

    struct DescriptorInfo {
        uint16_t tag;
        uint32_t location;
        uint32_t length;
    };

    bool read_bytes(uint64_t byte_offset, uint8_t* buffer, size_t size);
    bool read_block(uint32_t lba, uint8_t* buffer);
    bool read_blocks(uint32_t lba, uint32_t count, uint8_t* buffer);
    bool read_extent(const Extent& extent, uint64_t offset,
                     uint8_t* buffer, size_t size);

    bool read_tag(uint32_t lba, uint16_t* tag_id);
    bool validate_tag(const uint8_t* block, uint16_t expected) const;
    bool find_avdp(uint32_t* main_lba, uint32_t* main_len,
                   uint32_t* reserve_lba, uint32_t* reserve_len);
    bool scan_volume_descriptors(uint32_t start_lba, uint32_t byte_length,
                                 bool* found_partition,
                                 bool* found_logical_volume,
                                 bool* found_fsd);
    bool parse_partition_descriptor(const uint8_t* block);
    bool parse_logical_volume_descriptor(const uint8_t* block);
    bool load_file_set_descriptor();

    bool read_file_entry(uint32_t location, uint32_t partition,
                         uint8_t** entry, size_t* entry_size,
                         uint8_t* file_type = nullptr,
                         uint64_t* file_size = nullptr,
                         uint32_t* flags = nullptr);
    bool get_file_extents(uint32_t location, uint32_t partition,
                          Extent* extents, size_t capacity, size_t* count,
                          uint64_t* file_size, bool* directory);

    bool read_directory_bytes(uint32_t location, uint32_t partition,
                              uint8_t** data, size_t* size);
    bool find_in_directory(uint32_t location, uint32_t partition,
                           const char* component, FileInfo* out);
    bool resolve_path(const char* path, FileInfo* out);

    bool parse_directory_record(const uint8_t* record, size_t available,
                                FileInfo* out, size_t* record_length);
    bool decode_file_identifier(const uint8_t* id, size_t id_length,
                                uint8_t file_characteristics,
                                char* out, size_t out_size);
    bool decode_dstring(const uint8_t* data, size_t length,
                        char* out, size_t out_size);
    bool equal_name(const char* a, const char* b) const;
    bool path_component(const char* path, size_t* offset,
                        char* component, size_t component_size) const;

    uint16_t le16(const uint8_t* p) const;
    uint32_t le32(const uint8_t* p) const;
    uint64_t le64(const uint8_t* p) const;

private:
    bool initialized_;
    bool mounted_;
    uint32_t logical_block_size_;
    uint32_t volume_space_size_;
    uint32_t partition_start_;
    uint32_t partition_length_;
    uint16_t partition_number_;
    uint32_t fsd_location_;
    uint16_t fsd_partition_;
    uint32_t root_icb_location_;
    uint16_t root_icb_partition_;
};

extern UDFReader udf_fs;

bool udf_init();
bool udf_read_file(const char* path, uint8_t* dest, size_t max_size,
                   size_t* bytes_read = nullptr);

} // namespace udf
