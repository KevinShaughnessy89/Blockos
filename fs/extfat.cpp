#include "exfat.hpp"
#include "vfs_blk_adapter.hpp"
#include "kernel/allocator.hpp"
#include <new>

namespace exfat {

static constexpr uint32_t PHYSICAL_SECTOR_SIZE = 512;
static constexpr uint32_t MIN_CLUSTER = 2;
static constexpr uint32_t MAX_CLUSTER = 0xFFFFFFF6U;
static constexpr uint32_t BAD_CLUSTER = 0xFFFFFFF7U;
static constexpr uint32_t FAT_EOC = 0xFFFFFFF8U;
static constexpr uint32_t FREE_CLUSTER = 0x00000000U;
static constexpr uint32_t MAX_PATH = 1024;

static uint16_t le16(const uint8_t* p)
{
    return static_cast<uint16_t>(p[0]) |
           (static_cast<uint16_t>(p[1]) << 8);
}

static uint32_t le32(const uint8_t* p)
{
    return static_cast<uint32_t>(p[0]) |
           (static_cast<uint32_t>(p[1]) << 8) |
           (static_cast<uint32_t>(p[2]) << 16) |
           (static_cast<uint32_t>(p[3]) << 24);
}

static uint64_t le64(const uint8_t* p)
{
    return static_cast<uint64_t>(p[0]) |
           (static_cast<uint64_t>(p[1]) << 8) |
           (static_cast<uint64_t>(p[2]) << 16) |
           (static_cast<uint64_t>(p[3]) << 24) |
           (static_cast<uint64_t>(p[4]) << 32) |
           (static_cast<uint64_t>(p[5]) << 40) |
           (static_cast<uint64_t>(p[6]) << 48) |
           (static_cast<uint64_t>(p[7]) << 56);
}

static bool ascii_equal_ci(const char* a, const char* b)
{
    if (!a || !b)
        return false;

    while (*a && *b) {
        char ca = *a;
        char cb = *b;
        if (ca >= 'a' && ca <= 'z')
            ca = static_cast<char>(ca - 'a' + 'A');
        if (cb >= 'a' && cb <= 'z')
            cb = static_cast<char>(cb - 'a' + 'A');
        if (ca != cb)
            return false;
        ++a;
        ++b;
    }

    return *a == '\0' && *b == '\0';
}

static size_t cstr_len(const char* s)
{
    if (!s)
        return 0;
    size_t n = 0;
    while (s[n])
        ++n;
    return n;
}

static void zero_memory(void* ptr, size_t size)
{
    uint8_t* p = static_cast<uint8_t*>(ptr);
    for (size_t i = 0; i < size; ++i)
        p[i] = 0;
}

static void copy_memory(void* dst, const void* src, size_t size)
{
    uint8_t* d = static_cast<uint8_t*>(dst);
    const uint8_t* s = static_cast<const uint8_t*>(src);
    for (size_t i = 0; i < size; ++i)
        d[i] = s[i];
}

// Do not instantiate ExFatReader as a global C++ object.
// BlockOS is a freestanding EFI kernel and does not provide the normal
// C++ runtime symbol __dso_handle used by global constructors/destructors.
// Keep only raw aligned storage at static scope and construct the reader
// explicitly when exFAT is first used.
alignas(ExFatReader) static uint8_t g_exfat_storage[sizeof(ExFatReader)];
static bool g_exfat_constructed = false;

static ExFatReader* get_exfat_reader()
{
    if (!g_exfat_constructed) {
        ::new (static_cast<void*>(g_exfat_storage)) ExFatReader();
        g_exfat_constructed = true;
    }
    return reinterpret_cast<ExFatReader*>(g_exfat_storage);
}

ExFatReader::ExFatReader()
    : volume_{}, ready_(false), cluster_buffer_(nullptr),
      cluster_buffer_size_(0)
{
}

ExFatReader::~ExFatReader()
{
    unmount();
}

bool ExFatReader::read_sector(uint64_t sector, uint8_t* buffer)
{
    return vfs_blk_adapter::read_sector_to_vfs(sector, buffer);
}

bool ExFatReader::read_sectors(uint64_t sector, uint32_t count,
                               uint8_t* buffer)
{
    return vfs_blk_adapter::read_sectors_to_vfs(sector, count, buffer);
}

bool ExFatReader::read_bytes(uint64_t offset, uint32_t size, uint8_t* buffer)
{
    if (!buffer || size == 0)
        return false;

    const uint64_t first_sector = offset / PHYSICAL_SECTOR_SIZE;
    const uint32_t in_sector = static_cast<uint32_t>(
        offset % PHYSICAL_SECTOR_SIZE);
    const uint64_t total = static_cast<uint64_t>(in_sector) + size;
    const uint32_t sectors = static_cast<uint32_t>(
        (total + PHYSICAL_SECTOR_SIZE - 1) / PHYSICAL_SECTOR_SIZE);

    uint8_t* temporary = static_cast<uint8_t*>(
        allocator::alloc(static_cast<size_t>(sectors) * PHYSICAL_SECTOR_SIZE,
                         16));
    if (!temporary)
        return false;

    const bool ok = read_sectors(first_sector, sectors, temporary);
    if (ok)
        copy_memory(buffer, temporary + in_sector, size);

    allocator::free(temporary);
    return ok;
}

bool ExFatReader::cluster_valid(uint32_t cluster) const
{
    if (!ready_)
        return false;
    if (cluster < MIN_CLUSTER)
        return false;
    if (cluster > MAX_CLUSTER)
        return false;
    return cluster < volume_.cluster_count + 2U;
}

uint64_t ExFatReader::cluster_sector(uint32_t cluster) const
{
    return static_cast<uint64_t>(volume_.partition_offset) +
           volume_.cluster_heap_offset +
           static_cast<uint64_t>(cluster - 2U) * volume_.sectors_per_cluster;
}

uint64_t ExFatReader::data_offset(uint32_t cluster, uint64_t offset) const
{
    return cluster_sector(cluster) * PHYSICAL_SECTOR_SIZE + offset;
}

bool ExFatReader::read_cluster(uint32_t cluster, uint8_t* buffer)
{
    if (!buffer || !cluster_valid(cluster))
        return false;

    return read_sectors(cluster_sector(cluster),
                        volume_.sectors_per_cluster, buffer);
}

bool ExFatReader::read_fat_entry(uint32_t cluster, uint32_t* next)
{
    if (!next || cluster < MIN_CLUSTER || cluster > MAX_CLUSTER)
        return false;

    const uint64_t fat_byte_offset =
        static_cast<uint64_t>(volume_.fat_offset) +
        static_cast<uint64_t>(cluster) * 4ULL;
    uint8_t bytes[4];

    if (!read_bytes((volume_.partition_offset * PHYSICAL_SECTOR_SIZE) +
                        fat_byte_offset * 1ULL,
                    sizeof(bytes), bytes))
        return false;

    *next = le32(bytes) & 0x0FFFFFFFU;
    return true;
}

bool ExFatReader::next_cluster(uint32_t current, uint32_t* next) const
{
    if (!next || !cluster_valid(current))
        return false;

    // const_cast is avoided by reading the FAT through a local helper.
    ExFatReader* self = const_cast<ExFatReader*>(this);
    return self->read_fat_entry(current, next);
}

bool ExFatReader::validate_boot_sector(const uint8_t* boot) const
{
    if (!boot)
        return false;

    // exFAT has no x86 boot jump requirement, but the filesystem name,
    // reserved fields and signature provide a useful minimum sanity check.
    static const char name[8] = {'E', 'X', 'F', 'A', 'T', ' ', ' ', ' '};
    for (size_t i = 0; i < 8; ++i) {
        if (boot[3 + i] != static_cast<uint8_t>(name[i]))
            return false;
    }

    if (le16(boot + 106) > 1)
        return false;

    if (boot[108] < 9 || boot[108] > 12)
        return false;

    if (boot[109] > 25)
        return false;

    if (boot[110] == 0 || boot[110] > 2)
        return false;

    if (le16(boot + 510) != 0xAA55)
        return false;

    return true;
}

bool ExFatReader::load_boot_sector()
{
    uint8_t boot[PHYSICAL_SECTOR_SIZE];
    if (!read_sector(0, boot))
        return false;

    if (!validate_boot_sector(boot))
        return false;

    volume_.partition_offset = le64(boot + 64);
    volume_.volume_length = le64(boot + 72);
    volume_.fat_offset = le32(boot + 80);
    volume_.fat_length = le32(boot + 84);
    volume_.cluster_heap_offset = le32(boot + 88);
    volume_.cluster_count = le32(boot + 92);
    volume_.root_cluster = le32(boot + 96);
    volume_.volume_serial = le32(boot + 100);
    volume_.revision = le16(boot + 104);
    volume_.volume_flags = le16(boot + 106);
    volume_.bytes_per_sector = 1U << boot[108];
    volume_.sectors_per_cluster = 1U << boot[109];
    volume_.fat_count = boot[110];

    if (volume_.bytes_per_sector != PHYSICAL_SECTOR_SIZE)
        return false;

    if (volume_.sectors_per_cluster == 0)
        return false;

    if (volume_.cluster_count == 0 || volume_.cluster_count > MAX_CLUSTER)
        return false;

    if (volume_.root_cluster < MIN_CLUSTER ||
        volume_.root_cluster >= volume_.cluster_count + 2U)
        return false;

    if (volume_.fat_offset == 0 || volume_.fat_length == 0 ||
        volume_.cluster_heap_offset == 0)
        return false;

    const uint64_t fat_end = static_cast<uint64_t>(volume_.fat_offset) +
                             static_cast<uint64_t>(volume_.fat_length) *
                             volume_.fat_count;
    if (fat_end > volume_.volume_length)
        return false;

    const uint64_t heap_end =
        static_cast<uint64_t>(volume_.cluster_heap_offset) +
        static_cast<uint64_t>(volume_.cluster_count) *
            volume_.sectors_per_cluster;
    if (heap_end > volume_.volume_length)
        return false;

    volume_.cluster_size = volume_.bytes_per_sector *
                           volume_.sectors_per_cluster;
    return volume_.cluster_size != 0;
}

bool ExFatReader::initialize()
{
    unmount();

    if (!vfs_blk_adapter::init_backend())
        return false;

    if (!load_boot_sector())
        return false;

    cluster_buffer_size_ = volume_.cluster_size;
    cluster_buffer_ = static_cast<uint8_t*>(
        allocator::alloc(cluster_buffer_size_, 16));
    if (!cluster_buffer_) {
        cluster_buffer_size_ = 0;
        return false;
    }

    ready_ = true;

    // Verify that the root cluster can actually be read.
    if (!read_cluster(volume_.root_cluster, cluster_buffer_)) {
        unmount();
        return false;
    }

    return true;
}

bool ExFatReader::mount()
{
    return initialize();
}

void ExFatReader::unmount()
{
    ready_ = false;
    if (cluster_buffer_) {
        allocator::free(cluster_buffer_);
        cluster_buffer_ = nullptr;
    }
    cluster_buffer_size_ = 0;
    zero_memory(&volume_, sizeof(volume_));
}

bool ExFatReader::ready() const
{
    return ready_;
}

bool ExFatReader::is_exfat() const
{
    if (!ready_)
        return false;
    return volume_.cluster_count != 0 && volume_.root_cluster >= MIN_CLUSTER;
}

const VolumeInfo& ExFatReader::volume_info() const
{
    return volume_;
}

void ExFatReader::copy_string(char* dst, size_t dst_size,
                              const char* src) const
{
    if (!dst || dst_size == 0)
        return;

    size_t i = 0;
    if (src) {
        while (src[i] && i + 1 < dst_size) {
            dst[i] = src[i];
            ++i;
        }
    }
    dst[i] = '\0';
}

uint16_t ExFatReader::fold_utf16(uint16_t c) const
{
    // exFAT's full case-folding table is stored as an UPCASE file. The
    // driver deliberately keeps the bootable core dependency-free here and
    // handles ASCII plus the common Latin-1 uppercase range directly.
    if (c >= 'a' && c <= 'z')
        return static_cast<uint16_t>(c - 'a' + 'A');

    if (c >= 0x00E0 && c <= 0x00F6)
        return static_cast<uint16_t>(c - 0x20);
    if (c >= 0x00F8 && c <= 0x00FE)
        return static_cast<uint16_t>(c - 0x20);

    return c;
}

void ExFatReader::decode_utf16_name(const uint8_t* entries,
                                    uint8_t name_length, char* out,
                                    size_t out_size) const
{
    if (!out || out_size == 0) {
        return;
    }

    out[0] = '\0';
    if (!entries)
        return;

    size_t written = 0;
    for (uint8_t i = 0; i < name_length; ++i) {
        const uint16_t cp = le16(entries + static_cast<size_t>(i) * 2U);
        if (cp == 0)
            break;

        uint32_t value = cp;
        if (written + 1 >= out_size)
            break;

        if (cp >= 0xD800 && cp <= 0xDBFF && i + 1 < name_length) {
            const uint16_t low = le16(entries +
                static_cast<size_t>(i + 1U) * 2U);
            if (low >= 0xDC00 && low <= 0xDFFF) {
                value = 0x10000U +
                    ((static_cast<uint32_t>(cp) - 0xD800U) << 10) +
                    (static_cast<uint32_t>(low) - 0xDC00U);
                ++i;
            }
        }

        if (value < 0x80U) {
            if (written + 1 >= out_size)
                break;
            out[written++] = static_cast<char>(value);
        } else if (value < 0x800U) {
            if (written + 2 >= out_size)
                break;
            out[written++] = static_cast<char>(0xC0U | (value >> 6));
            out[written++] = static_cast<char>(0x80U | (value & 0x3FU));
        } else if (value < 0x10000U) {
            if (written + 3 >= out_size)
                break;
            out[written++] = static_cast<char>(0xE0U | (value >> 12));
            out[written++] = static_cast<char>(0x80U | ((value >> 6) & 0x3FU));
            out[written++] = static_cast<char>(0x80U | (value & 0x3FU));
        } else {
            if (written + 4 >= out_size)
                break;
            out[written++] = static_cast<char>(0xF0U | (value >> 18));
            out[written++] = static_cast<char>(0x80U | ((value >> 12) & 0x3FU));
            out[written++] = static_cast<char>(0x80U | ((value >> 6) & 0x3FU));
            out[written++] = static_cast<char>(0x80U | (value & 0x3FU));
        }
    }

    out[written] = '\0';
}

bool ExFatReader::compare_name(const char* a, const char* b) const
{
    if (!a || !b)
        return false;

    return ascii_equal_ci(a, b);
}

bool ExFatReader::decode_file_entry(const uint8_t* entry,
                                    const uint8_t* directory,
                                    uint32_t directory_offset,
                                    FileInfo* out)
{
    if (!entry || !directory || !out)
        return false;

    if (entry[0] != ENTRY_FILE)
        return false;

    const uint8_t secondary_count = entry[1];
    if (secondary_count < 2 || secondary_count > 20)
        return false;

    const uint32_t required =
        static_cast<uint32_t>(secondary_count + 1U) * EXFAT_ENTRY_SIZE;
    if (directory_offset + required > volume_.cluster_size)
        return false;

    const uint16_t attributes = le16(entry + 4);
    const uint8_t* stream = nullptr;
    const uint8_t* names[20];
    uint8_t name_entries = 0;

    uint32_t offset = directory_offset + EXFAT_ENTRY_SIZE;
    for (uint8_t i = 0; i < secondary_count; ++i) {
        const uint8_t* e = directory + offset;
        if (e[0] == ENTRY_STREAM && !stream) {
            stream = e;
        } else if (e[0] == ENTRY_NAME && name_entries < 20) {
            names[name_entries++] = e;
        }
        offset += EXFAT_ENTRY_SIZE;
    }

    if (!stream || name_entries == 0)
        return false;

    const uint8_t name_length = stream[3];
    if (name_length == 0 || name_length > 255)
        return false;

    const uint32_t first_cluster = le32(stream + 20);
    const uint64_t data_length = le64(stream + 24);
    const bool no_fat_chain = (stream[1] & 0x02U) != 0;

    zero_memory(out, sizeof(*out));
    out->first_cluster = first_cluster;
    out->size = data_length;
    out->attributes = attributes;
    out->name_length = name_length;
    out->directory = (attributes & 0x0010U) != 0;
    out->no_fat_chain = no_fat_chain;

    size_t written = 0;
    for (uint8_t n = 0; n < name_entries && written < name_length; ++n) {
        const uint8_t chars = static_cast<uint8_t>(
            (name_length - written) > 15 ? 15 : (name_length - written));
        char temp[64];
        decode_utf16_name(names[n] + 2, chars, temp, sizeof(temp));
        const size_t len = cstr_len(temp);
        for (size_t j = 0; j < len && written < sizeof(out->name) - 1; ++j)
            out->name[written++] = temp[j];
    }
    out->name[written] = '\0';

    return true;
}

bool ExFatReader::find_in_directory(uint32_t directory_cluster,
                                    const char* wanted, FileInfo* out)
{
    if (!ready_ || !wanted || !out || !cluster_valid(directory_cluster))
        return false;

    uint32_t cluster = directory_cluster;
    uint32_t guard = 0;

    while (cluster_valid(cluster) && guard++ <= volume_.cluster_count) {
        if (!read_cluster(cluster, cluster_buffer_))
            return false;

        uint32_t offset = 0;
        while (offset + EXFAT_ENTRY_SIZE <= volume_.cluster_size) {
            const uint8_t* entry = cluster_buffer_ + offset;
            const uint8_t type = entry[0];

            if (type == ENTRY_END)
                return false;

            if (type == ENTRY_FILE) {
                FileInfo info;
                if (decode_file_entry(entry, cluster_buffer_, offset, &info) &&
                    compare_name(info.name, wanted)) {
                    *out = info;
                    return true;
                }
                offset += static_cast<uint32_t>(entry[1] + 1U) *
                          EXFAT_ENTRY_SIZE;
                continue;
            }

            offset += EXFAT_ENTRY_SIZE;
        }

        uint32_t next = 0;
        if (!read_fat_entry(cluster, &next))
            return false;
        if (next >= FAT_EOC)
            break;
        if (next == BAD_CLUSTER || next == FREE_CLUSTER ||
            !cluster_valid(next))
            return false;
        cluster = next;
    }

    return false;
}

bool ExFatReader::find_path(const char* path, FileInfo* out)
{
    if (!ready_ || !path || !out)
        return false;

    while (*path == '/')
        ++path;

    if (*path == '\0') {
        zero_memory(out, sizeof(*out));
        out->name[0] = '/';
        out->name[1] = '\0';
        out->first_cluster = volume_.root_cluster;
        out->directory = true;
        out->attributes = 0x0010;
        return true;
    }

    char component[260];
    uint32_t current_cluster = volume_.root_cluster;
    const char* cursor = path;

    for (;;) {
        while (*cursor == '/')
            ++cursor;
        if (*cursor == '\0')
            break;

        size_t len = 0;
        while (cursor[len] && cursor[len] != '/') {
            if (len + 1 >= sizeof(component))
                return false;
            ++len;
        }
        component[len] = '\0';

        FileInfo found;
        if (!find_in_directory(current_cluster, component, &found))
            return false;

        cursor += len;
        while (*cursor == '/')
            ++cursor;

        if (*cursor == '\0') {
            *out = found;
            return true;
        }

        if (!found.directory || !cluster_valid(found.first_cluster))
            return false;

        current_cluster = found.first_cluster;
    }

    return false;
}

bool ExFatReader::find(const char* path, FileInfo* out)
{
    return find_path(path, out);
}

bool ExFatReader::stat(const char* path, FileInfo* out)
{
    return find(path, out);
}

bool ExFatReader::read_file_data(const FileInfo& file, uint8_t* dest,
                                 size_t max_size, size_t* bytes_read)
{
    if (bytes_read)
        *bytes_read = 0;

    if (file.directory || !dest || max_size == 0)
        return false;

    if (file.size > max_size)
        return false;

    if (file.size == 0) {
        if (bytes_read)
            *bytes_read = 0;
        return true;
    }

    if (!cluster_valid(file.first_cluster))
        return false;

    uint64_t remaining = file.size;
    size_t destination_offset = 0;
    uint32_t cluster = file.first_cluster;
    uint32_t guard = 0;

    while (remaining != 0) {
        if (!cluster_valid(cluster) || guard++ > volume_.cluster_count)
            return false;

        if (!read_cluster(cluster, cluster_buffer_))
            return false;

        const uint32_t chunk = static_cast<uint32_t>(
            remaining > volume_.cluster_size ? volume_.cluster_size : remaining);
        copy_memory(dest + destination_offset, cluster_buffer_, chunk);
        destination_offset += chunk;
        remaining -= chunk;

        if (remaining == 0)
            break;

        uint32_t next = 0;
        if (file.no_fat_chain) {
            next = cluster + 1U;
        } else {
            if (!read_fat_entry(cluster, &next))
                return false;
        }

        if (next >= FAT_EOC || next == BAD_CLUSTER ||
            !cluster_valid(next))
            return false;
        cluster = next;
    }

    if (bytes_read)
        *bytes_read = destination_offset;
    return true;
}

bool ExFatReader::read_file(const char* path, uint8_t* dest, size_t max_size,
                            size_t* bytes_read)
{
    FileInfo file;
    if (!find(path, &file))
        return false;

    return read_file_data(file, dest, max_size, bytes_read);
}

bool ExFatReader::read_directory_entries(uint32_t directory_cluster,
                                         FileInfo* entries, size_t capacity,
                                         size_t* count)
{
    if (count)
        *count = 0;

    if (!ready_ || !entries || capacity == 0 ||
        !cluster_valid(directory_cluster))
        return false;

    size_t found_count = 0;
    uint32_t cluster = directory_cluster;
    uint32_t guard = 0;

    while (cluster_valid(cluster) && guard++ <= volume_.cluster_count) {
        if (!read_cluster(cluster, cluster_buffer_))
            return false;

        uint32_t offset = 0;
        while (offset + EXFAT_ENTRY_SIZE <= volume_.cluster_size) {
            const uint8_t* entry = cluster_buffer_ + offset;
            const uint8_t type = entry[0];

            if (type == ENTRY_END) {
                if (count)
                    *count = found_count;
                return true;
            }

            if (type == ENTRY_FILE) {
                FileInfo info;
                if (decode_file_entry(entry, cluster_buffer_, offset, &info)) {
                    if (found_count < capacity)
                        entries[found_count++] = info;
                    else {
                        if (count)
                            *count = found_count;
                        return false;
                    }
                }
                offset += static_cast<uint32_t>(entry[1] + 1U) *
                          EXFAT_ENTRY_SIZE;
                continue;
            }

            offset += EXFAT_ENTRY_SIZE;
        }

        uint32_t next = 0;
        if (!read_fat_entry(cluster, &next))
            return false;
        if (next >= FAT_EOC)
            break;
        if (next == BAD_CLUSTER || next == FREE_CLUSTER ||
            !cluster_valid(next))
            return false;
        cluster = next;
    }

    if (count)
        *count = found_count;
    return true;
}

bool ExFatReader::list_directory(const char* path, FileInfo* entries,
                                 size_t capacity, size_t* count)
{
    FileInfo directory;
    if (!find(path, &directory))
        return false;

    if (!directory.directory)
        return false;

    return read_directory_entries(directory.first_cluster, entries,
                                  capacity, count);
}

bool ExFatReader::write_file(const char* path, const uint8_t* data,
                             size_t size)
{
    (void)path;
    (void)data;
    (void)size;
    return false;
}

bool exfat_init()
{
    return get_exfat_reader()->initialize();
}

bool exfat_read_file(const char* path, uint8_t* dest, size_t max_size,
                     size_t* bytes_read)
{
    ExFatReader* reader = get_exfat_reader();
    if (!reader->ready() && !reader->initialize())
        return false;
    return reader->read_file(path, dest, max_size, bytes_read);
}

} // namespace exfat
