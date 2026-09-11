#include "fs/ntfs.hpp"
#include "fs/vfs_blk_adapter.hpp"
#include "kernel/allocator.hpp"

#include <cstddef>
#include <cstdint>

namespace {

constexpr uint32_t SECTOR_SIZE = 512;
constexpr uint32_t ATTR_STANDARD_INFORMATION = 0x10;
constexpr uint32_t ATTR_FILE_NAME = 0x30;
constexpr uint32_t ATTR_DATA = 0x80;
constexpr uint32_t ATTR_INDEX_ROOT = 0x90;
constexpr uint32_t ATTR_END = 0xffffffffu;
constexpr uint64_t MFT_BAD = ~uint64_t(0);

struct NtfsState {
    bool ready;
    uint16_t bps;
    uint8_t spc;
    uint64_t total;
    uint64_t mft_cluster;
    uint64_t mftmirr_cluster;
    uint32_t record_size;
    uint32_t cluster_size;
};

NtfsState g{};

static uint16_t rd16(const uint8_t* p) {
    return uint16_t(p[0]) | (uint16_t(p[1]) << 8);
}
static uint32_t rd32(const uint8_t* p) {
    return uint32_t(p[0]) | (uint32_t(p[1]) << 8) |
           (uint32_t(p[2]) << 16) | (uint32_t(p[3]) << 24);
}
static uint64_t rd64(const uint8_t* p) {
    uint64_t v = 0;
    for (unsigned i = 0; i < 8; ++i) v |= uint64_t(p[i]) << (i * 8);
    return v;
}
static int64_t rdsi(const uint8_t* p) { return static_cast<int64_t>(rd64(p)); }

static bool ascii_equal_ci(const char* a, const char* b) {
    if (!a || !b) return false;
    while (*a && *b) {
        char ca = *a, cb = *b;
        if (ca >= 'A' && ca <= 'Z') ca = char(ca + 32);
        if (cb >= 'A' && cb <= 'Z') cb = char(cb + 32);
        if (ca != cb) return false;
        ++a; ++b;
    }
    return *a == *b;
}

static bool read_sectors(uint64_t sector, uint32_t count, void* out) {
    if (!out || !count) return false;
    return vfs_blk_adapter::read_sectors_to_vfs(
        sector, count, static_cast<uint8_t*>(out));
}

static bool read_bytes(uint64_t byte_offset, void* out, uint32_t size) {
    if (!out || !size || g.bps == 0) return false;
    uint64_t first = byte_offset / g.bps;
    uint32_t in = uint32_t(byte_offset % g.bps);
    uint32_t need = (in + size + g.bps - 1) / g.bps;
    uint8_t* tmp = static_cast<uint8_t*>(allocator::alloc(
        size_t(need) * g.bps, 16));
    if (!tmp) return false;
    bool ok = read_sectors(first, need, tmp);
    if (ok) {
        for (uint32_t i = 0; i < size; ++i)
            static_cast<uint8_t*>(out)[i] = tmp[in + i];
    }
    allocator::free(tmp);
    return ok;
}

static uint64_t cluster_byte(uint64_t cluster) {
    return cluster * uint64_t(g.cluster_size);
}

static bool apply_usa(uint8_t* rec, uint32_t size) {
    if (!rec || size < 48) return false;
    if (rd32(rec) != 0x454c4946u) return false; // FILE
    uint16_t usa_off = rd16(rec + 4);
    uint16_t usa_count = rd16(rec + 6);
    if (usa_off < 8 || uint32_t(usa_off) + uint32_t(usa_count) * 2 > size)
        return false;
    if (usa_count < 2) return false;

    uint16_t seq = rd16(rec + usa_off);
    for (uint16_t i = 1; i < usa_count; ++i) {
        uint32_t pos = uint32_t(i) * g.bps - 2;
        if (pos + 2 > size) return false;
        if (rd16(rec + pos) != seq) return false;
        rec[pos] = rec[usa_off + i * 2];
        rec[pos + 1] = rec[usa_off + i * 2 + 1];
    }
    return true;
}

struct Run {
    uint64_t lcn;
    uint64_t length;
};

static bool decode_runs(const uint8_t* mapping, uint32_t mapping_len,
                        uint32_t start_vcn, uint64_t wanted_vcn,
                        Run* out, uint32_t max_runs, uint32_t* out_count) {
    if (!mapping || !out || !out_count || !mapping_len) return false;
    uint32_t pos = 0, count = 0;
    uint64_t vcn = start_vcn;
    int64_t lcn = 0;
    while (pos < mapping_len) {
        uint8_t h = mapping[pos++];
        if (!h) break;
        uint32_t len_sz = h & 0x0f;
        uint32_t off_sz = h >> 4;
        if (!len_sz || len_sz > 8 || off_sz > 8 || pos + len_sz + off_sz > mapping_len)
            return false;
        uint64_t run_len = 0;
        for (uint32_t i = 0; i < len_sz; ++i)
            run_len |= uint64_t(mapping[pos + i]) << (8 * i);
        pos += len_sz;
        int64_t delta = 0;
        if (off_sz) {
            uint64_t raw = 0;
            for (uint32_t i = 0; i < off_sz; ++i)
                raw |= uint64_t(mapping[pos + i]) << (8 * i);
            if (mapping[pos + off_sz - 1] & 0x80) {
                for (uint32_t i = off_sz; i < 8; ++i)
                    raw |= uint64_t(0xff) << (8 * i);
            }
            delta = static_cast<int64_t>(raw);
        }
        pos += off_sz;
        if (delta < 0 && uint64_t(-delta) > uint64_t(lcn)) return false;
        lcn += delta;
        if (vcn + run_len > wanted_vcn && count < max_runs) {
            uint64_t skip = wanted_vcn > vcn ? wanted_vcn - vcn : 0;
            out[count].lcn = lcn < 0 ? MFT_BAD : uint64_t(lcn) + skip;
            out[count].length = run_len - skip;
            ++count;
            wanted_vcn = vcn + run_len;
        }
        vcn += run_len;
    }
    *out_count = count;
    return count != 0;
}

static bool read_record(uint64_t number, uint8_t* rec) {
    if (!rec || !g.record_size) return false;
    uint64_t off = cluster_byte(g.mft_cluster) + number * uint64_t(g.record_size);
    return read_bytes(off, rec, g.record_size) && apply_usa(rec, g.record_size);
}

struct Attr {
    const uint8_t* p;
    uint32_t size;
    bool nonresident;
};

static bool next_attr(const uint8_t* rec, uint32_t rec_size, uint32_t& off, Attr* out) {
    if (!out || off + 16 > rec_size) return false;
    const uint8_t* a = rec + off;
    uint32_t type = rd32(a);
    if (type == ATTR_END) return false;
    uint32_t len = rd32(a + 4);
    if (len < 24 || off + len > rec_size) return false;
    out->p = a;
    out->size = len;
    out->nonresident = a[8] != 0;
    off += len;
    return true;
}

static bool attr_find(const uint8_t* rec, uint32_t rec_size, uint32_t type, Attr* out) {
    uint32_t off = rd16(rec + 20);
    if (off < 24 || off >= rec_size) return false;
    Attr a{};
    while (next_attr(rec, rec_size, off, &a)) {
        if (rd32(a.p) == type) {
            *out = a;
            return true;
        }
    }
    return false;
}

static uint64_t attr_size(const Attr& a) {
    if (!a.nonresident) return rd32(a.p + 16);
    return rd64(a.p + 48);
}

static bool read_nonresident(const Attr& a, uint64_t offset, void* out, uint32_t size) {
    if (!a.nonresident || !out || !size) return false;
    uint64_t data_size = rd64(a.p + 48);
    if (offset >= data_size) return false;
    if (offset + size > data_size) size = uint32_t(data_size - offset);
    uint32_t mapping_off = rd16(a.p + 32);
    uint32_t mapping_len = a.size > mapping_off ? a.size - mapping_off : 0;
    if (mapping_off >= a.size || !mapping_len) return false;

    uint64_t cluster = offset / g.cluster_size;
    uint32_t in_cluster = uint32_t(offset % g.cluster_size);
    uint8_t* dst = static_cast<uint8_t*>(out);
    uint32_t left = size;
    while (left) {
        Run run[4]{}; uint32_t nr = 0;
        if (!decode_runs(a.p + mapping_off, mapping_len, 0, cluster, run, 4, &nr))
            return false;
        if (!nr || run[0].lcn == MFT_BAD) return false;
        uint64_t phys = run[0].lcn;
        uint64_t available = run[0].length * uint64_t(g.cluster_size);
        if (available <= in_cluster) return false;
        available -= in_cluster;
        uint32_t take = left;
        if (uint64_t(take) > available) take = uint32_t(available);
        uint64_t byte = cluster_byte(phys) + in_cluster;
        if (!read_bytes(byte, dst, take)) return false;
        dst += take; left -= take; cluster += (in_cluster + take) / g.cluster_size;
        in_cluster = uint32_t((in_cluster + take) % g.cluster_size);
        if (!take) return false;
    }
    return true;
}

static bool read_attr(const Attr& a, uint64_t offset, void* out, uint32_t size) {
    if (!a.nonresident) {
        uint32_t len = rd32(a.p + 16), off = rd16(a.p + 20);
        if (offset >= len || off + offset + size > a.size) return false;
        return read_bytes(0, out, 0) || [&]() {
            const uint8_t* src = a.p + off + offset;
            for (uint32_t i = 0; i < size; ++i) static_cast<uint8_t*>(out)[i] = src[i];
            return true;
        }();
    }
    return read_nonresident(a, offset, out, size);
}

static uint32_t utf16_to_ascii(const uint8_t* p, uint32_t bytes, char* out, uint32_t cap) {
    if (!out || cap == 0) return 0;
    uint32_t n = 0;
    for (uint32_t i = 0; i + 1 < bytes && n + 1 < cap; i += 2) {
        uint16_t c = rd16(p + i);
        if (c == 0) break;
        if (c < 0x80) out[n++] = char(c);
        else if (c < 0x100) out[n++] = '?';
        else out[n++] = '?';
    }
    out[n] = 0;
    return n;
}

static bool record_filename(const uint8_t* rec, char* name, uint32_t cap,
                            uint64_t* parent, uint64_t* size, bool* directory) {
    Attr a{};
    if (!attr_find(rec, g.record_size, ATTR_FILE_NAME, &a) || a.nonresident) return false;
    uint32_t len = rd32(a.p + 16), off = rd16(a.p + 20);
    if (off + len > a.size || len < 66) return false;
    const uint8_t* f = a.p + off;
    if (parent) *parent = rd64(f) & 0x0000ffffffffffffULL;
    if (size) *size = rd64(f + 48);
    if (directory) *directory = (f[56] & 0x02) != 0;
    uint32_t name_len = f[64];
    if (66 + name_len * 2 > len) return false;
    utf16_to_ascii(f + 66, name_len * 2, name, cap);
    return true;
}

static bool find_record_by_name(const char* name, uint64_t parent, uint64_t* found) {
    if (!name || !found) return false;
    uint8_t* rec = static_cast<uint8_t*>(allocator::alloc(g.record_size, 16));
    if (!rec) return false;
    // $MFT is normally the first file record. Scan conservatively; this avoids
    // requiring a full INDEX_ALLOCATION implementation for basic read access.
    const uint64_t max_records = g.total * uint64_t(g.bps) / g.record_size;
    const uint64_t limit = max_records > 131072 ? 131072 : max_records;
    char current[256];
    bool ok = false;
    for (uint64_t i = 0; i < limit; ++i) {
        if (!read_record(i, rec)) continue;
        uint16_t flags = rd16(rec + 22);
        if (!(flags & 1)) continue;
        uint64_t p = 0, sz = 0; bool dir = false;
        current[0] = 0;
        if (!record_filename(rec, current, sizeof(current), &p, &sz, &dir)) continue;
        if (p == parent && ascii_equal_ci(current, name)) {
            *found = i; ok = true; break;
        }
    }
    allocator::free(rec);
    return ok;
}

static bool path_component(const char*& p, char* out, uint32_t cap) {
    while (*p == '/') ++p;
    if (!*p) return false;
    uint32_t n = 0;
    while (*p && *p != '/') {
        if (n + 1 < cap) out[n++] = *p;
        ++p;
    }
    out[n] = 0;
    return n != 0;
}

static bool resolve_path(const char* path, uint64_t* rec_no, ntfs::FileInfo* info) {
    if (!path || !rec_no) return false;
    while (*path == '/') ++path;
    uint64_t current = 5; // NTFS root directory record.
    if (!*path) {
        if (info) { info->mft_record = 5; info->directory = true; info->size = 0; info->name[0] = '/'; info->name[1] = 0; }
        *rec_no = current; return true;
    }
    char component[256];
    while (path_component(path, component, sizeof(component))) {
        uint64_t next = 0;
        if (!find_record_by_name(component, current, &next)) return false;
        current = next;
    }
    if (info) {
        uint8_t* rec = static_cast<uint8_t*>(allocator::alloc(g.record_size, 16));
        if (!rec) return false;
        bool ok = read_record(current, rec);
        if (ok) {
            uint64_t parent = 0, sz = 0; bool dir = false;
            ok = record_filename(rec, info->name, sizeof(info->name), &parent, &sz, &dir);
            info->mft_record = current; info->size = sz; info->directory = dir;
        }
        allocator::free(rec);
        if (!ok) return false;
    }
    *rec_no = current;
    return true;
}

} // namespace

namespace ntfs {

bool init() {
    g = {};
    if (!vfs_blk_adapter::init_backend()) return false;
    uint8_t boot[SECTOR_SIZE];
    if (!read_sectors(0, 1, boot)) return false;
    if (boot[3] != 'N' || boot[4] != 'T' || boot[5] != 'F' || boot[6] != 'S') return false;
    uint16_t bps = rd16(boot + 11);
    uint8_t spc = boot[13];
    if (!bps || (bps & (bps - 1)) || !spc || (spc & (spc - 1))) return false;
    int8_t cpr = static_cast<int8_t>(boot[64]);
    uint32_t record_size;
    if (cpr < 0) {
        uint8_t shift = uint8_t(-cpr);
        if (shift >= 31) return false;
        record_size = 1u << shift;
    } else {
        record_size = uint32_t(cpr) * uint32_t(bps) * uint32_t(spc);
    }
    if (record_size < 512 || record_size > 1024 * 1024) return false;
    g.bps = bps; g.spc = spc; g.cluster_size = uint32_t(bps) * spc;
    g.total = rd64(boot + 40); g.mft_cluster = rd64(boot + 48);
    g.mftmirr_cluster = rd64(boot + 56); g.record_size = record_size;
    if (!g.total || g.cluster_size == 0) { g = {}; return false; }
    uint8_t* rec = static_cast<uint8_t*>(allocator::alloc(g.record_size, 16));
    if (!rec) { g = {}; return false; }
    bool ok = read_record(0, rec);
    allocator::free(rec);
    if (!ok) { g = {}; return false; }
    g.ready = true;
    return true;
}

bool probe() { return init(); }
bool mounted() { return g.ready; }
uint32_t bytes_per_sector() { return g.bps; }
uint32_t bytes_per_cluster() { return g.cluster_size; }
uint32_t file_record_size() { return g.record_size; }
uint64_t total_sectors() { return g.total; }
uint64_t mft_record() { return g.mft_cluster; }

bool find(const char* path, FileInfo* out) {
    if (!g.ready) return false;
    uint64_t rec = 0;
    return resolve_path(path, &rec, out);
}

bool read_file(const char* path, uint64_t offset, void* buffer, uint32_t size,
               uint32_t* out_read) {
    if (out_read) *out_read = 0;
    if (!g.ready || !buffer || !size) return false;
    FileInfo info{}; uint64_t rec_no = 0;
    if (!resolve_path(path, &rec_no, &info) || info.directory) return false;
    if (offset >= info.size) return true;
    if (uint64_t(size) > info.size - offset) size = uint32_t(info.size - offset);
    uint8_t* rec = static_cast<uint8_t*>(allocator::alloc(g.record_size, 16));
    if (!rec || !read_record(rec_no, rec)) { if (rec) allocator::free(rec); return false; }
    Attr data{};
    bool ok = attr_find(rec, g.record_size, ATTR_DATA, &data) && read_attr(data, offset, buffer, size);
    allocator::free(rec);
    if (ok && out_read) *out_read = size;
    return ok;
}

bool list_directory(const char* path,
                    bool (*callback)(const FileInfo& info, void* user),
                    void* user) {
    if (!g.ready || !callback) return false;
    FileInfo dir{}; uint64_t rec_no = 0;
    if (!resolve_path(path, &rec_no, &dir) || !dir.directory) return false;
    // This compact driver enumerates $FILE_NAME records by parent reference.
    // It intentionally does not yet parse NTFS B+tree index blocks.
    uint8_t* rec = static_cast<uint8_t*>(allocator::alloc(g.record_size, 16));
    if (!rec) return false;
    const uint64_t max_records = g.total * uint64_t(g.bps) / g.record_size;
    const uint64_t limit = max_records > 131072 ? 131072 : max_records;
    bool ok = true;
    for (uint64_t i = 0; i < limit; ++i) {
        if (!read_record(i, rec)) continue;
        if (!(rd16(rec + 22) & 1)) continue;
        FileInfo fi{}; uint64_t parent = 0, sz = 0; bool isdir = false;
        if (!record_filename(rec, fi.name, sizeof(fi.name), &parent, &sz, &isdir)) continue;
        if (parent != rec_no || fi.name[0] == 0) continue;
        fi.mft_record = i; fi.size = sz; fi.directory = isdir;
        if (!callback(fi, user)) { ok = false; break; }
    }
    allocator::free(rec);
    return ok;
}

} // namespace ntfs
