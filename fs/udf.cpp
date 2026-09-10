#include "udf.hpp"
#include "vfs_blk_adapter.hpp"

#include <cstddef>
#include <cstdint>

namespace udf {

namespace {
constexpr uint32_t DEFAULT_BLOCK_SIZE = 2048;
constexpr uint32_t AVDP_LOCATION = 256;
constexpr size_t MAX_EXTENTS = 64;
constexpr size_t MAX_DIRECTORY_SIZE = 4 * 1024 * 1024;

static void mem_copy(void* dst, const void* src, size_t n) {
    uint8_t* d = static_cast<uint8_t*>(dst);
    const uint8_t* s = static_cast<const uint8_t*>(src);
    for (size_t i = 0; i < n; ++i) d[i] = s[i];
}

static void mem_zero(void* dst, size_t n) {
    uint8_t* d = static_cast<uint8_t*>(dst);
    for (size_t i = 0; i < n; ++i) d[i] = 0;
}

static size_t str_len(const char* s) {
    if (!s) return 0;
    size_t n = 0;
    while (s[n]) ++n;
    return n;
}

static bool str_equal(const char* a, const char* b) {
    if (!a || !b) return false;
    size_t i = 0;
    while (a[i] && b[i]) {
        if (a[i] != b[i]) return false;
        ++i;
    }
    return a[i] == b[i];
}

static char ascii_lower(char c) {
    return (c >= 'A' && c <= 'Z') ? static_cast<char>(c + ('a' - 'A')) : c;
}

static bool name_equal_ci(const char* a, const char* b) {
    size_t i = 0;
    while (a[i] && b[i]) {
        if (ascii_lower(a[i]) != ascii_lower(b[i])) return false;
        ++i;
    }
    return a[i] == b[i];
}

static size_t utf8_write(uint32_t cp, char* out, size_t pos, size_t cap) {
    if (cp <= 0x7F) {
        if (pos + 1 >= cap) return pos;
        out[pos++] = static_cast<char>(cp);
    } else if (cp <= 0x7FF) {
        if (pos + 2 >= cap) return pos;
        out[pos++] = static_cast<char>(0xC0 | (cp >> 6));
        out[pos++] = static_cast<char>(0x80 | (cp & 0x3F));
    } else if (cp <= 0xFFFF) {
        if (pos + 3 >= cap) return pos;
        out[pos++] = static_cast<char>(0xE0 | (cp >> 12));
        out[pos++] = static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
        out[pos++] = static_cast<char>(0x80 | (cp & 0x3F));
    } else {
        if (pos + 4 >= cap) return pos;
        out[pos++] = static_cast<char>(0xF0 | (cp >> 18));
        out[pos++] = static_cast<char>(0x80 | ((cp >> 12) & 0x3F));
        out[pos++] = static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
        out[pos++] = static_cast<char>(0x80 | (cp & 0x3F));
    }
    return pos;
}

static bool append_char(char* out, size_t cap, size_t* pos, char c) {
    if (!out || !pos || *pos + 1 >= cap) return false;
    out[(*pos)++] = c;
    out[*pos] = '\0';
    return true;
}

} // namespace

UDFReader udf_fs;

UDFReader::UDFReader()
    : initialized_(false), mounted_(false), logical_block_size_(DEFAULT_BLOCK_SIZE),
      volume_space_size_(0), partition_start_(0), partition_length_(0),
      partition_number_(0), fsd_location_(0), fsd_partition_(0),
      root_icb_location_(0), root_icb_partition_(0) {}

uint16_t UDFReader::le16(const uint8_t* p) const {
    return static_cast<uint16_t>(p[0]) |
           (static_cast<uint16_t>(p[1]) << 8);
}

uint32_t UDFReader::le32(const uint8_t* p) const {
    return static_cast<uint32_t>(p[0]) |
           (static_cast<uint32_t>(p[1]) << 8) |
           (static_cast<uint32_t>(p[2]) << 16) |
           (static_cast<uint32_t>(p[3]) << 24);
}

uint64_t UDFReader::le64(const uint8_t* p) const {
    uint64_t v = 0;
    for (unsigned i = 0; i < 8; ++i) v |= static_cast<uint64_t>(p[i]) << (i * 8);
    return v;
}

bool UDFReader::read_bytes(uint64_t byte_offset, uint8_t* buffer, size_t size) {
    if (!buffer) return false;
    if (size == 0) return true;
    const uint64_t first = byte_offset / SECTOR_SIZE;
    const uint32_t sector_off = static_cast<uint32_t>(byte_offset % SECTOR_SIZE);
    const uint64_t total = static_cast<uint64_t>(sector_off) + size;
    const uint64_t sectors64 = (total + SECTOR_SIZE - 1) / SECTOR_SIZE;
    if (sectors64 > 0xFFFFFFFFULL) return false;
    const uint32_t sectors = static_cast<uint32_t>(sectors64);

    if (sector_off == 0 && size % SECTOR_SIZE == 0) {
        return vfs_blk_adapter::read_sectors_to_vfs(first, sectors, buffer);
    }

    uint8_t* temp = static_cast<uint8_t*>(::operator new[](static_cast<size_t>(sectors) * SECTOR_SIZE));
    if (!temp) return false;
    const bool ok = vfs_blk_adapter::read_sectors_to_vfs(first, sectors, temp);
    if (ok) mem_copy(buffer, temp + sector_off, size);
    ::operator delete[](temp);
    return ok;
}

bool UDFReader::read_block(uint32_t lba, uint8_t* buffer) {
    if (!buffer || logical_block_size_ == 0 || logical_block_size_ % SECTOR_SIZE != 0) return false;
    const uint32_t sectors = logical_block_size_ / SECTOR_SIZE;
    const uint64_t sector = static_cast<uint64_t>(lba) * sectors;
    return vfs_blk_adapter::read_sectors_to_vfs(sector, sectors, buffer);
}

bool UDFReader::read_blocks(uint32_t lba, uint32_t count, uint8_t* buffer) {
    if (!buffer || count == 0 || logical_block_size_ == 0 || logical_block_size_ % SECTOR_SIZE != 0) return false;
    const uint32_t sectors_per_block = logical_block_size_ / SECTOR_SIZE;
    const uint64_t sector = static_cast<uint64_t>(lba) * sectors_per_block;
    const uint64_t sectors = static_cast<uint64_t>(count) * sectors_per_block;
    if (sectors > 0xFFFFFFFFULL) return false;
    return vfs_blk_adapter::read_sectors_to_vfs(
        sector, static_cast<uint32_t>(sectors), buffer);
}

bool UDFReader::read_extent(const Extent& extent, uint64_t offset,
                            uint8_t* buffer, size_t size) {
    if (!buffer) return false;
    if (offset > extent.length) return false;
    if (size > static_cast<uint64_t>(extent.length) - offset) return false;
    const uint64_t byte = static_cast<uint64_t>(extent.location) * logical_block_size_ + offset;
    return read_bytes(byte, buffer, size);
}

bool UDFReader::validate_tag(const uint8_t* block, uint16_t expected) const {
    if (!block) return false;
    const uint16_t id = static_cast<uint16_t>(block[0]) |
                        (static_cast<uint16_t>(block[1]) << 8);
    if (id != expected) return false;
    const uint16_t crc_len = static_cast<uint16_t>(block[10]) |
                             (static_cast<uint16_t>(block[11]) << 8);
    const uint16_t crc = static_cast<uint16_t>(block[8]) |
                         (static_cast<uint16_t>(block[9]) << 8);
    if (crc_len == 0) return true;
    uint16_t value = 0;
    for (uint32_t i = 0; i < crc_len; ++i) {
        const uint8_t b = block[16 + i];
        const uint8_t x = static_cast<uint8_t>(value >> 8);
        value = static_cast<uint16_t>((value << 8) ^ b);
        for (int bit = 0; bit < 8; ++bit) {
            if (value & 0x8000) value = static_cast<uint16_t>((value << 1) ^ 0x1021);
            else value = static_cast<uint16_t>(value << 1);
        }
        (void)x;
    }
    return value == crc;
}

bool UDFReader::read_tag(uint32_t lba, uint16_t* tag_id) {
    if (!tag_id) return false;
    uint8_t* block = static_cast<uint8_t*>(::operator new[](logical_block_size_));
    if (!block) return false;
    const bool ok = read_block(lba, block);
    if (ok) *tag_id = le16(block);
    ::operator delete[](block);
    return ok;
}

bool UDFReader::find_avdp(uint32_t* main_lba, uint32_t* main_len,
                          uint32_t* reserve_lba, uint32_t* reserve_len) {
    if (!main_lba || !main_len || !reserve_lba || !reserve_len) return false;
    *main_lba = *main_len = *reserve_lba = *reserve_len = 0;

    uint8_t* block = static_cast<uint8_t*>(::operator new[](logical_block_size_));
    if (!block) return false;

    const uint32_t candidates[] = { AVDP_LOCATION, 512, 1024 };
    bool found = false;
    for (size_t i = 0; i < sizeof(candidates) / sizeof(candidates[0]); ++i) {
        if (!read_block(candidates[i], block)) continue;
        if (le16(block) != TAG_AVDP) continue;
        const uint32_t ml = le32(block + 16);
        const uint32_t mn = le32(block + 20);
        const uint32_t rl = le32(block + 24);
        const uint32_t rn = le32(block + 28);
        if (mn == 0) continue;
        *main_lba = ml;
        *main_len = mn;
        *reserve_lba = rl;
        *reserve_len = rn;
        found = true;
        break;
    }
    ::operator delete[](block);
    return found;
}

bool UDFReader::parse_partition_descriptor(const uint8_t* block) {
    if (!block || le16(block) != TAG_PARTITION_DESCRIPTOR) return false;
    partition_number_ = le16(block + 22);
    partition_start_ = le32(block + 188);
    partition_length_ = le32(block + 192);
    return partition_length_ != 0;
}

bool UDFReader::parse_logical_volume_descriptor(const uint8_t* block) {
    if (!block || le16(block) != TAG_LOGICAL_VOLUME_DESCRIPTOR) return false;
    const uint32_t bs = le32(block + 212);
    if (bs == 0 || bs % SECTOR_SIZE != 0 || bs > 32768) return false;
    logical_block_size_ = bs;
    // Logical Volume Contents Use contains the long_ad pointing to the
    // first File Set Descriptor sequence (ECMA-167 3/10.6.7).
    fsd_location_ = le32(block + 252);
    fsd_partition_ = le16(block + 256);
    return fsd_location_ != 0;
}

bool UDFReader::scan_volume_descriptors(uint32_t start_lba, uint32_t byte_length,
                                        bool* found_partition,
                                        bool* found_logical_volume,
                                        bool* found_fsd) {
    if (!found_partition || !found_logical_volume || !found_fsd) return false;
    *found_partition = *found_logical_volume = *found_fsd = false;

    uint8_t* block = static_cast<uint8_t*>(::operator new[](logical_block_size_));
    if (!block) return false;
    const uint32_t count = (byte_length + logical_block_size_ - 1) / logical_block_size_;
    bool terminated = false;
    for (uint32_t i = 0; i < count; ++i) {
        if (!read_block(start_lba + i, block)) break;
        const uint16_t tag = le16(block);
        if (tag == TAG_PARTITION_DESCRIPTOR) {
            if (parse_partition_descriptor(block)) *found_partition = true;
        } else if (tag == TAG_LOGICAL_VOLUME_DESCRIPTOR) {
            if (parse_logical_volume_descriptor(block)) *found_logical_volume = true;
        } else if (tag == TAG_TERMINATING_DESCRIPTOR) {
            terminated = true;
            break;
        }
    }
    ::operator delete[](block);
    *found_fsd = (fsd_location_ != 0);
    return terminated || (*found_partition && *found_logical_volume);
}

bool UDFReader::load_file_set_descriptor() {
    if (fsd_location_ == 0 || partition_length_ == 0) return false;
    uint8_t* block = static_cast<uint8_t*>(::operator new[](logical_block_size_));
    if (!block) return false;
    uint64_t absolute = static_cast<uint64_t>(partition_start_) + fsd_location_;
    bool ok = read_block(static_cast<uint32_t>(absolute), block);
    if (ok && le16(block) == TAG_FILE_SET_DESCRIPTOR) {
        // Root Directory ICB is a long_ad at FSD offset 400.
        root_icb_location_ = le32(block + 404);
        root_icb_partition_ = le16(block + 408);
        ok = root_icb_location_ != 0;
    } else {
        ok = false;
    }
    ::operator delete[](block);
    return ok;
}

bool UDFReader::initialize() {
    initialized_ = false;
    mounted_ = false;
    logical_block_size_ = DEFAULT_BLOCK_SIZE;
    volume_space_size_ = 0;
    partition_start_ = partition_length_ = 0;
    partition_number_ = 0;
    fsd_location_ = root_icb_location_ = 0;
    fsd_partition_ = root_icb_partition_ = 0;

    if (!vfs_blk_adapter::init_backend()) return false;

    // UDF commonly uses 2048-byte logical blocks. Read the AVDP with that size first.
    uint8_t* block = static_cast<uint8_t*>(::operator new[](DEFAULT_BLOCK_SIZE));
    if (!block) return false;
    bool ok = vfs_blk_adapter::read_sectors_to_vfs(
        static_cast<uint64_t>(AVDP_LOCATION) * 4, 4, block);
    if (!ok || le16(block) != TAG_AVDP) {
        ::operator delete[](block);
        return false;
    }
    ::operator delete[](block);

    uint32_t main_lba = 0, main_len = 0, reserve_lba = 0, reserve_len = 0;
    if (!find_avdp(&main_lba, &main_len, &reserve_lba, &reserve_len)) return false;
    (void)reserve_lba;
    (void)reserve_len;

    bool fp = false, fl = false, ff = false;
    if (!scan_volume_descriptors(main_lba, main_len, &fp, &fl, &ff)) return false;
    if (!fp || !fl || !ff) return false;

    // The initial BlockOS UDF reader supports the standard Type-1 partition map.
    // The LVD's partition reference is therefore the physical partition number.
    if (fsd_partition_ != partition_number_) return false;
    if (!load_file_set_descriptor()) return false;

    initialized_ = true;
    mounted_ = true;
    return true;
}

bool UDFReader::mount() {
    return initialize();
}

void UDFReader::unmount() {
    initialized_ = false;
    mounted_ = false;
}

bool UDFReader::ready() const { return initialized_ && mounted_; }
bool UDFReader::is_udf() const { return ready(); }
uint32_t UDFReader::logical_block_size() const { return logical_block_size_; }
uint32_t UDFReader::partition_start() const { return partition_start_; }
uint32_t UDFReader::partition_length() const { return partition_length_; }

bool UDFReader::read_file_entry(uint32_t location, uint32_t partition,
                                uint8_t** entry, size_t* entry_size,
                                uint8_t* file_type, uint64_t* file_size,
                                uint32_t* flags) {
    if (!entry || !entry_size || partition != partition_number_) return false;
    if (location >= partition_length_) return false;

    uint8_t* block = static_cast<uint8_t*>(::operator new[](logical_block_size_));
    if (!block) return false;
    const uint64_t absolute = static_cast<uint64_t>(partition_start_) + location;
    bool ok = read_block(static_cast<uint32_t>(absolute), block);
    if (!ok) {
        ::operator delete[](block);
        return false;
    }

    const uint16_t tag = le16(block);
    if (tag != TAG_FILE_ENTRY && tag != TAG_EXTENDED_FILE_ENTRY) {
        ::operator delete[](block);
        return false;
    }

    uint64_t size = 0;
    uint32_t ad_length = 0;
    uint32_t ad_offset = 0;
    uint32_t icb_flags = 0;
    uint8_t type = block[16];

    if (tag == TAG_FILE_ENTRY) {
        size = le64(block + 56);
        icb_flags = le16(block + 34);
        ad_length = le32(block + 168);
        ad_offset = 176;
    } else {
        size = le64(block + 56);
        icb_flags = le16(block + 34);
        ad_length = le32(block + 212);
        ad_offset = 216;
    }

    const size_t needed = static_cast<size_t>(ad_offset) + ad_length;
    if (needed > logical_block_size_ || ad_length > 1024 * 1024) {
        ::operator delete[](block);
        return false;
    }

    uint8_t* copy = static_cast<uint8_t*>(::operator new[](needed));
    if (!copy) {
        ::operator delete[](block);
        return false;
    }
    mem_copy(copy, block, needed);
    ::operator delete[](block);

    *entry = copy;
    *entry_size = needed;
    if (file_type) *file_type = type;
    if (file_size) *file_size = size;
    if (flags) *flags = icb_flags;
    return true;
}

bool UDFReader::get_file_extents(uint32_t location, uint32_t partition,
                                 Extent* extents, size_t capacity, size_t* count,
                                 uint64_t* file_size, bool* directory) {
    if (!extents || !count) return false;
    *count = 0;
    if (file_size) *file_size = 0;
    if (directory) *directory = false;

    uint8_t* entry = nullptr;
    size_t entry_size = 0;
    uint8_t type = 0;
    uint64_t size = 0;
    uint32_t flags = 0;
    if (!read_file_entry(location, partition, &entry, &entry_size,
                         &type, &size, &flags)) return false;

    const uint16_t tag = le16(entry);
    const uint32_t ad_length = (tag == TAG_FILE_ENTRY) ? le32(entry + 168) : le32(entry + 212);
    const uint32_t ad_offset = (tag == TAG_FILE_ENTRY) ? 176 : 216;
    const uint32_t ad_type = flags & 0x0007;
    const bool is_dir = (type == 4);
    if (directory) *directory = is_dir;
    if (file_size) *file_size = size;

    size_t pos = ad_offset;
    const size_t end = static_cast<size_t>(ad_offset) + ad_length;
    while (pos + 8 <= end && *count < capacity) {
        uint32_t length = 0;
        uint32_t lbn = 0;
        uint16_t part = partition_number_;
        if (ad_type == 0) { // short_ad
            length = le32(entry + pos) & 0x3FFFFFFF;
            lbn = le32(entry + pos + 4);
            pos += 8;
        } else if (ad_type == 1) { // long_ad
            if (pos + 16 > end) break;
            length = le32(entry + pos) & 0x3FFFFFFF;
            lbn = le32(entry + pos + 4);
            part = le16(entry + pos + 8);
            pos += 16;
        } else {
            // Extended ADs are uncommon for ordinary UDF files. Reject rather
            // than misinterpreting their layout.
            ::operator delete[](entry);
            return false;
        }
        if (length == 0) continue;
        if (part != partition_number_ || lbn >= partition_length_) {
            ::operator delete[](entry);
            return false;
        }
        extents[*count].location = partition_start_ + lbn;
        extents[*count].partition = part;
        extents[*count].length = length;
        ++(*count);
    }

    ::operator delete[](entry);
    return *count != 0 || size == 0;
}

bool UDFReader::read_directory_bytes(uint32_t location, uint32_t partition,
                                     uint8_t** data, size_t* size) {
    if (!data || !size) return false;
    *data = nullptr;
    *size = 0;
    Extent extents[MAX_EXTENTS];
    size_t count = 0;
    uint64_t file_size = 0;
    bool directory = false;
    if (!get_file_extents(location, partition, extents, MAX_EXTENTS, &count,
                          &file_size, &directory)) return false;
    if (!directory || file_size > MAX_DIRECTORY_SIZE) return false;
    if (file_size == 0) {
        *data = nullptr;
        *size = 0;
        return true;
    }

    uint8_t* buffer = static_cast<uint8_t*>(::operator new[](static_cast<size_t>(file_size)));
    if (!buffer) return false;
    uint64_t copied = 0;
    for (size_t i = 0; i < count && copied < file_size; ++i) {
        const uint64_t n = (file_size - copied < extents[i].length)
                               ? file_size - copied : extents[i].length;
        if (!read_extent(extents[i], 0, buffer + copied, static_cast<size_t>(n))) {
            ::operator delete[](buffer);
            return false;
        }
        copied += n;
    }
    if (copied != file_size) {
        ::operator delete[](buffer);
        return false;
    }
    *data = buffer;
    *size = static_cast<size_t>(file_size);
    return true;
}

bool UDFReader::decode_dstring(const uint8_t* data, size_t length,
                               char* out, size_t out_size) {
    if (!data || !out || out_size == 0 || length == 0) return false;
    const uint8_t comp = data[0];
    size_t pos = 0;
    size_t out_pos = 0;
    if (comp == 8) {
        pos = 1;
        while (pos < length && out_pos + 1 < out_size) {
            const uint8_t c = data[pos++];
            if (c == 0) break;
            out[out_pos++] = static_cast<char>(c);
        }
    } else if (comp == 16) {
        pos = 1;
        while (pos + 1 < length && out_pos + 1 < out_size) {
            const uint16_t cp = static_cast<uint16_t>((data[pos] << 8) | data[pos + 1]);
            pos += 2;
            if (cp == 0) break;
            out_pos = utf8_write(cp, out, out_pos, out_size);
        }
    } else {
        return false;
    }
    out[out_pos] = '\0';
    return true;
}

bool UDFReader::decode_file_identifier(const uint8_t* id, size_t id_length,
                                       uint8_t file_characteristics,
                                       char* out, size_t out_size) {
    if (!id || !out || out_size == 0) return false;
    out[0] = '\0';
    if (id_length == 1 && id[0] == 0) {
        out[0] = '.'; out[1] = '\0'; return true;
    }
    if (id_length == 1 && id[0] == 1) {
        out[0] = '.'; out[1] = '.'; out[2] = '\0'; return true;
    }
    (void)file_characteristics;
    return decode_dstring(id, id_length, out, out_size);
}

bool UDFReader::parse_directory_record(const uint8_t* record, size_t available,
                                       FileInfo* out, size_t* record_length) {
    if (!record || !out || !record_length || available < 38) return false;
    if (le16(record) != TAG_FILE_IDENTIFIER_DESCRIPTOR) return false;

    const uint8_t file_char = record[18];
    const uint8_t id_len = record[19];
    const uint16_t imp_len = le16(record + 36);
    const size_t id_offset = 38 + imp_len;
    if (id_offset > available || id_len > available - id_offset) return false;

    size_t raw_len = id_offset + id_len;
    // A File Identifier Descriptor is padded to an even byte boundary.
    if (raw_len & 1U) ++raw_len;
    if (raw_len > available) return false;

    const uint32_t icb_location = le32(record + 24);
    const uint16_t partition = le16(record + 28);
    if (partition != partition_number_) return false;

    out->icb_location = icb_location;
    out->partition = partition;
    out->file_type = 0;
    out->size = 0;
    out->directory = (file_char & 0x02U) != 0;
    out->name[0] = '\0';

    if (file_char & 0x04U) {
        // Deleted FIDs are not visible to normal directory enumeration.
        *record_length = raw_len;
        return true;
    }

    if (!decode_file_identifier(record + id_offset, id_len, file_char,
                                out->name, sizeof(out->name))) return false;

    if ((file_char & 0x08U) != 0) {
        // Parent entry is represented by a one-byte identifier in UDF.
        out->name[0] = '.';
        out->name[1] = '.';
        out->name[2] = '\0';
    }

    uint8_t* entry = nullptr;
    size_t entry_size = 0;
    uint8_t type = 0;
    uint64_t size = 0;
    if (!read_file_entry(icb_location, partition, &entry, &entry_size,
                         &type, &size, nullptr)) return false;
    out->file_type = type;
    out->size = size;
    out->directory = (type == 4) || ((file_char & 0x02U) != 0);
    ::operator delete[](entry);
    *record_length = raw_len;
    return true;
}

bool UDFReader::find_in_directory(uint32_t location, uint32_t partition,
                                  const char* component, FileInfo* out) {
    if (!component || !out) return false;
    uint8_t* data = nullptr;
    size_t size = 0;
    if (!read_directory_bytes(location, partition, &data, &size)) return false;

    size_t pos = 0;
    while (pos + 38 <= size) {
        const uint32_t len = le32(data + pos + 16);
        if (len == 0) {
            const size_t next = ((pos / logical_block_size_) + 1) * logical_block_size_;
            pos = (next > pos) ? next : size;
            continue;
        }
        if (len > size - pos) break;
        FileInfo candidate;
        size_t consumed = 0;
        if (parse_directory_record(data + pos, size - pos, &candidate, &consumed)) {
            if (candidate.name[0] != '.' && equal_name(candidate.name, component)) {
                *out = candidate;
                ::operator delete[](data);
                return true;
            }
        }
        pos += len;
    }
    ::operator delete[](data);
    return false;
}

bool UDFReader::path_component(const char* path, size_t* offset,
                               char* component, size_t component_size) const {
    if (!path || !offset || !component || component_size < 2) return false;
    size_t p = *offset;
    while (path[p] == '/') ++p;
    if (!path[p]) return false;
    size_t n = 0;
    while (path[p] && path[p] != '/') {
        if (n + 1 >= component_size) return false;
        component[n++] = path[p++];
    }
    component[n] = '\0';
    *offset = p;
    return true;
}

bool UDFReader::resolve_path(const char* path, FileInfo* out) {
    if (!ready() || !path || !out) return false;
    size_t off = 0;
    char component[256];
    FileInfo current{};
    current.icb_location = root_icb_location_;
    current.partition = root_icb_partition_;
    current.size = 0;
    current.file_type = 4;
    current.directory = true;
    current.name[0] = '/'; current.name[1] = '\0';

    bool got = false;
    while (path_component(path, &off, component, sizeof(component))) {
        if (str_equal(component, ".")) continue;
        if (str_equal(component, "..")) {
            // UDF directory records contain parent ICB information, but keeping
            // a parent stack is safer than attempting to synthesize it here.
            return false;
        }
        if (!current.directory) return false;
        FileInfo next;
        if (!find_in_directory(current.icb_location, current.partition, component, &next)) return false;
        current = next;
        got = true;
    }
    if (got) *out = current;
    else *out = current;
    return true;
}

bool UDFReader::find(const char* path, FileInfo* out) {
    return resolve_path(path, out);
}

bool UDFReader::stat(const char* path, FileInfo* out) {
    return find(path, out);
}

bool UDFReader::read_file(const char* path, uint8_t* dest, size_t max_size,
                          size_t* bytes_read) {
    if (bytes_read) *bytes_read = 0;
    if (!dest) return false;
    FileInfo info;
    if (!find(path, &info) || info.directory) return false;
    if (info.size > max_size) return false;

    Extent extents[MAX_EXTENTS];
    size_t count = 0;
    uint64_t file_size = 0;
    bool directory = false;
    if (!get_file_extents(info.icb_location, info.partition, extents, MAX_EXTENTS,
                          &count, &file_size, &directory)) return false;
    if (directory || file_size > max_size) return false;

    uint64_t copied = 0;
    for (size_t i = 0; i < count && copied < file_size; ++i) {
        const uint64_t n = (file_size - copied < extents[i].length)
                               ? file_size - copied : extents[i].length;
        if (!read_extent(extents[i], 0, dest + copied, static_cast<size_t>(n))) return false;
        copied += n;
    }
    if (copied != file_size) return false;
    if (bytes_read) *bytes_read = static_cast<size_t>(copied);
    return true;
}

bool UDFReader::list_directory(const char* path, DirectoryEntry* entries,
                               size_t capacity, size_t* count) {
    if (!entries || !count) return false;
    *count = 0;
    FileInfo dir;
    if (!find(path, &dir) || !dir.directory) return false;

    uint8_t* data = nullptr;
    size_t size = 0;
    if (!read_directory_bytes(dir.icb_location, dir.partition, &data, &size)) return false;
    size_t pos = 0;
    while (pos + 38 <= size && *count < capacity) {
        const uint32_t len = le32(data + pos + 16);
        if (len == 0) {
            const size_t next = ((pos / logical_block_size_) + 1) * logical_block_size_;
            pos = (next > pos) ? next : size;
            continue;
        }
        if (len > size - pos) break;
        FileInfo item;
        size_t consumed = 0;
        if (parse_directory_record(data + pos, size - pos, &item, &consumed)) {
            if (!str_equal(item.name, ".") && !str_equal(item.name, "..")) {
                entries[*count].info = item;
                ++(*count);
            }
        }
        pos += len;
    }
    ::operator delete[](data);
    return true;
}

bool udf_init() {
    return udf_fs.initialize();
}

bool udf_read_file(const char* path, uint8_t* dest, size_t max_size,
                   size_t* bytes_read) {
    return udf_fs.read_file(path, dest, max_size, bytes_read);
}

} // namespace udf
