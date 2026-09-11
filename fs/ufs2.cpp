#include "ufs2.hpp"
#include "../drivers/virtio_blk.hpp"

namespace ufs2 {
namespace {

static constexpr uint64_t SECTOR_SIZE = 512;
static constexpr uint64_t SBLOCK_UFS2 = 65536;
static constexpr uint64_t SBLOCK_UFS1 = 8192;
static constexpr uint64_t SBLOCK_PIGGY = 262144;
static constexpr uint32_t SBLOCK_SIZE = 8192;
static constexpr uint32_t MAX_BLOCK = 131072;
static constexpr uint32_t MAX_PATH = 1024;
static constexpr uint32_t MAX_DIR_BUFFER = 131072;

// Offsets are the stable x86_64 UFS2 on-disk offsets used by BSD FFS.
// fs_magic is 1372 and fs_sblockloc is 1000 in the superblock.
static constexpr uint32_t SB_FS_IBLKNO = 16;
static constexpr uint32_t SB_FS_NCG = 44;
static constexpr uint32_t SB_FS_BSIZE = 48;
static constexpr uint32_t SB_FS_FSIZE = 52;
static constexpr uint32_t SB_FS_FRAG = 56;
static constexpr uint32_t SB_FS_SB_SIZE = 104;
static constexpr uint32_t SB_FS_NINDIR = 112;
static constexpr uint32_t SB_FS_INOPB = 116;
static constexpr uint32_t SB_FS_IPG = 184;
static constexpr uint32_t SB_FS_FPG = 188;
static constexpr uint32_t SB_FS_SBLOCKLOC = 1000;
static constexpr uint32_t SB_FS_SIZE = 1016;
static constexpr uint32_t SB_FS_DSIZE = 1024;
static constexpr uint32_t SB_FS_MAXSYMLINKLEN = 1320;
static constexpr uint32_t SB_FS_MAGIC = 1372;

static constexpr uint32_t CG_MAGIC = 0x090255u;

static SuperblockInfo g_sb{};
static bool g_initialized = false;
static uint64_t g_partition_sector = 0;
static bool g_big_endian = false;

// Reusable static buffers keep this freestanding and avoid depending on BlockOS malloc.
static uint8_t g_block[MAX_BLOCK];
static uint8_t g_indirect[MAX_BLOCK];
static uint8_t g_inode_block[MAX_BLOCK];
static uint8_t g_dir_block[MAX_BLOCK];
static uint8_t g_path[MAX_PATH];

static uint16_t bswap16(uint16_t v) {
    return static_cast<uint16_t>((v >> 8) | (v << 8));
}

static uint32_t bswap32(uint32_t v) {
    return ((v & 0x000000FFu) << 24) |
           ((v & 0x0000FF00u) << 8) |
           ((v & 0x00FF0000u) >> 8) |
           ((v & 0xFF000000u) >> 24);
}

static uint64_t bswap64(uint64_t v) {
    return (static_cast<uint64_t>(bswap32(static_cast<uint32_t>(v))) << 32) |
           bswap32(static_cast<uint32_t>(v >> 32));
}

static uint16_t rd16(const uint8_t* p) {
    uint16_t v = static_cast<uint16_t>(p[0]) |
                 (static_cast<uint16_t>(p[1]) << 8);
    return g_big_endian ? bswap16(v) : v;
}

static uint32_t rd32(const uint8_t* p) {
    uint32_t v = static_cast<uint32_t>(p[0]) |
                 (static_cast<uint32_t>(p[1]) << 8) |
                 (static_cast<uint32_t>(p[2]) << 16) |
                 (static_cast<uint32_t>(p[3]) << 24);
    return g_big_endian ? bswap32(v) : v;
}

static uint64_t rd64(const uint8_t* p) {
    uint64_t v = static_cast<uint64_t>(p[0]) |
                 (static_cast<uint64_t>(p[1]) << 8) |
                 (static_cast<uint64_t>(p[2]) << 16) |
                 (static_cast<uint64_t>(p[3]) << 24) |
                 (static_cast<uint64_t>(p[4]) << 32) |
                 (static_cast<uint64_t>(p[5]) << 40) |
                 (static_cast<uint64_t>(p[6]) << 48) |
                 (static_cast<uint64_t>(p[7]) << 56);
    return g_big_endian ? bswap64(v) : v;
}

static int64_t rd64s(const uint8_t* p) {
    return static_cast<int64_t>(rd64(p));
}

static bool add_overflow_u64(uint64_t a, uint64_t b, uint64_t* out) {
    if (a > UINT64_MAX - b) return true;
    *out = a + b;
    return false;
}

static bool mul_overflow_u64(uint64_t a, uint64_t b, uint64_t* out) {
    if (a != 0 && b > UINT64_MAX / a) return true;
    *out = a * b;
    return false;
}

static bool read_bytes(uint64_t byte_offset, uint8_t* out, size_t size) {
    if (!out && size != 0) return false;

    uint64_t absolute = 0;
    if (add_overflow_u64(g_partition_sector * SECTOR_SIZE, byte_offset, &absolute))
        return false;

    while (size != 0) {
        uint64_t sector = absolute / SECTOR_SIZE;
        uint32_t sector_off = static_cast<uint32_t>(absolute % SECTOR_SIZE);

        uint8_t sector_buf[SECTOR_SIZE];
        if (!virtio_blk::read_sector(sector, sector_buf)) return false;

        size_t n = SECTOR_SIZE - sector_off;
        if (n > size) n = size;

        for (size_t i = 0; i < n; ++i)
            out[i] = sector_buf[sector_off + i];

        out += n;
        size -= n;
        absolute += n;
    }
    return true;
}

static bool read_fs_block(uint64_t block, uint8_t* out) {
    if (!g_initialized && block != 0) return false;
    if (g_sb.block_size == 0 || g_sb.block_size > MAX_BLOCK) return false;

    uint64_t off = 0;
    if (mul_overflow_u64(block, g_sb.block_size, &off)) return false;
    return read_bytes(off, out, g_sb.block_size);
}

static bool valid_fs_block(uint64_t block) {
    return block != 0 && (g_sb.fs_size == 0 || block < g_sb.fs_size);
}

static bool parse_superblock(const uint8_t* sb, uint64_t location) {
    uint32_t raw = static_cast<uint32_t>(sb[SB_FS_MAGIC]) |
                   (static_cast<uint32_t>(sb[SB_FS_MAGIC + 1]) << 8) |
                   (static_cast<uint32_t>(sb[SB_FS_MAGIC + 2]) << 16) |
                   (static_cast<uint32_t>(sb[SB_FS_MAGIC + 3]) << 24);

    if (raw == UFS2_MAGIC) {
        g_big_endian = false;
    } else if (bswap32(raw) == UFS2_MAGIC) {
        g_big_endian = true;
    } else {
        return false;
    }

    uint32_t bsize = rd32(sb + SB_FS_BSIZE);
    uint32_t fsize = rd32(sb + SB_FS_FSIZE);
    uint32_t frag = rd32(sb + SB_FS_FRAG);
    uint32_t ipg = rd32(sb + SB_FS_IPG);
    uint32_t fpg = rd32(sb + SB_FS_FPG);
    uint32_t inopb = rd32(sb + SB_FS_INOPB);
    uint32_t nindir = rd32(sb + SB_FS_NINDIR);
    uint32_t ncg = rd32(sb + SB_FS_NCG);
    uint64_t sblockloc = rd64(sb + SB_FS_SBLOCKLOC);

    if (bsize < 4096 || bsize > MAX_BLOCK) return false;
    if (fsize == 0 || fsize > bsize) return false;
    if ((bsize % fsize) != 0) return false;
    if (frag == 0 || frag > 8) return false;
    if (bsize / fsize != frag) return false;
    if (ipg == 0 || fpg == 0 || ncg == 0) return false;
    if (inopb != bsize / UFS2_INODE_SIZE) return false;
    if (nindir != bsize / sizeof(uint64_t)) return false;

    // The standard UFS2 superblock location must agree with the candidate.
    if (sblockloc != location) return false;

    uint32_t sbsize = rd32(sb + SB_FS_SB_SIZE);
    if (sbsize < sizeof(uint32_t) * 100 || sbsize > SBLOCK_SIZE) return false;

    uint64_t fs_size = rd64(sb + SB_FS_SIZE);
    uint64_t fs_dsize = rd64(sb + SB_FS_DSIZE);

    g_sb.location = location;
    g_sb.fs_size = fs_size;
    g_sb.fs_dsize = fs_dsize;
    g_sb.block_size = bsize;
    g_sb.fragment_size = fsize;
    g_sb.fragments_per_block = frag;
    g_sb.inodes_per_group = ipg;
    g_sb.fragments_per_group = fpg;
    g_sb.inode_block = rd32(sb + SB_FS_IBLKNO);
    g_sb.inode_size = UFS2_INODE_SIZE;
    g_sb.inodes_per_block = inopb;
    g_sb.indirect_entries = nindir;
    g_sb.cylinder_groups = ncg;
    g_sb.big_endian = g_big_endian;

    if (g_sb.inode_block >= fpg) return false;
    return true;
}

static bool read_superblock_at(uint64_t location) {
    uint8_t sb[SBLOCK_SIZE];
    if (!read_bytes(location, sb, sizeof(sb))) return false;
    return parse_superblock(sb, location);
}

static bool read_indirect_entry(uint64_t block, uint64_t index, uint64_t* out) {
    if (!out || index >= g_sb.indirect_entries || !valid_fs_block(block)) return false;
    if (!read_fs_block(block, g_indirect)) return false;
    *out = rd64(g_indirect + index * sizeof(uint64_t));
    return true;
}

static bool map_file_block(const Inode& inode, uint64_t logical, uint64_t* physical) {
    if (!physical) return false;
    *physical = 0;

    const uint64_t n = g_sb.indirect_entries;
    if (logical < UFS2_NDADDR) {
        *physical = inode.direct[logical];
        return true;
    }

    logical -= UFS2_NDADDR;
    if (logical < n) {
        return read_indirect_entry(inode.indirect[0], logical, physical);
    }

    logical -= n;
    uint64_t n2 = n * n;
    if (logical < n2) {
        uint64_t first = logical / n;
        uint64_t second = logical % n;
        uint64_t block = 0;
        if (!read_indirect_entry(inode.indirect[1], first, &block)) return false;
        return read_indirect_entry(block, second, physical);
    }

    logical -= n2;
    uint64_t n3 = n2 * n;
    if (logical >= n3) return false;

    uint64_t first = logical / n2;
    uint64_t rem = logical % n2;
    uint64_t second = rem / n;
    uint64_t third = rem % n;

    uint64_t block1 = 0;
    uint64_t block2 = 0;
    if (!read_indirect_entry(inode.indirect[2], first, &block1)) return false;
    if (!read_indirect_entry(block1, second, &block2)) return false;
    return read_indirect_entry(block2, third, physical);
}

static bool read_inode_raw(uint64_t inode_number, uint8_t* raw) {
    if (!raw || !g_initialized) return false;
    if (inode_number == 0 || inode_number >=
        static_cast<uint64_t>(g_sb.inodes_per_group) * g_sb.cylinder_groups)
        return false;

    uint64_t cg = inode_number / g_sb.inodes_per_group;
    uint64_t local = inode_number % g_sb.inodes_per_group;

    // ino_to_fsba(fs, ino): cgstart + fs_iblkno + blkstofrags(inoblock).
    uint64_t inode_block_index = local / g_sb.inodes_per_block;
    uint64_t block = cg * g_sb.fragments_per_group +
                     g_sb.inode_block +
                     inode_block_index * g_sb.fragments_per_block;

    if (!valid_fs_block(block)) return false;
    if (!read_fs_block(block, g_inode_block)) return false;

    uint64_t offset = (local % g_sb.inodes_per_block) * UFS2_INODE_SIZE;
    if (offset + UFS2_INODE_SIZE > g_sb.block_size) return false;

    for (uint32_t i = 0; i < UFS2_INODE_SIZE; ++i)
        raw[i] = g_inode_block[offset + i];
    return true;
}

static bool parse_inode(const uint8_t* raw, Inode* out) {
    if (!raw || !out) return false;
    out->mode = rd16(raw + 0);
    out->nlink = rd16(raw + 2);
    out->uid = rd32(raw + 4);
    out->gid = rd32(raw + 8);
    out->block_size = rd32(raw + 12);
    out->size = rd64(raw + 16);
    out->blocks = rd64(raw + 24);
    out->atime = rd64s(raw + 32);
    out->mtime = rd64s(raw + 40);
    out->ctime = rd64s(raw + 48);
    out->birthtime = rd64s(raw + 56);
    out->generation = rd32(raw + 80);
    out->flags = rd32(raw + 88);

    for (uint32_t i = 0; i < UFS2_NDADDR; ++i)
        out->direct[i] = rd64(raw + 112 + i * 8);
    for (uint32_t i = 0; i < UFS2_NIADDR; ++i)
        out->indirect[i] = rd64(raw + 208 + i * 8);

    return true;
}

static size_t string_length(const char* s) {
    if (!s) return 0;
    size_t n = 0;
    while (s[n] != '\0' && n < MAX_PATH) ++n;
    return n;
}

static bool component_equal(const uint8_t* name, uint32_t length, const char* wanted) {
    size_t wl = string_length(wanted);
    if (wl != length) return false;
    for (size_t i = 0; i < wl; ++i) {
        if (static_cast<char>(name[i]) != wanted[i]) return false;
    }
    return true;
}

static bool lookup_in_directory(const Inode& dir, const char* wanted, uint64_t* out) {
    if ((dir.mode & IFMT) != IFDIR || !wanted || !out) return false;
    if (string_length(wanted) == 0 || string_length(wanted) > UFS2_MAX_NAME) return false;

    uint64_t offset = 0;
    while (offset < dir.size) {
        uint64_t lbn = offset / g_sb.block_size;
        uint32_t in_block = static_cast<uint32_t>(offset % g_sb.block_size);
        uint64_t physical = 0;
        if (!map_file_block(dir, lbn, &physical)) return false;

        if (physical == 0) {
            offset += g_sb.block_size - in_block;
            continue;
        }
        if (!read_fs_block(physical, g_dir_block)) return false;

        uint32_t available = g_sb.block_size - in_block;
        uint32_t pos = 0;
        while (pos + 12 <= available && offset + pos < dir.size) {
            const uint8_t* e = g_dir_block + in_block + pos;
            uint64_t ino = rd64(e + 0);
            uint16_t reclen = rd16(e + 8);
            uint8_t dtype = e[10];
            uint8_t namlen = e[11];
            (void)dtype;

            if (reclen < 12 || (reclen & 3u) != 0 || reclen > available - pos)
                return false;
            if (namlen > UFS2_MAX_NAME || 12u + namlen > reclen)
                return false;

            if (ino != 0 && component_equal(e + 12, namlen, wanted)) {
                *out = ino;
                return true;
            }

            pos += reclen;
        }

        offset += available;
    }
    return false;
}

static bool next_component(const char*& p, char* out, size_t out_size) {
    if (!p || !out || out_size < 2) return false;
    while (*p == '/') ++p;
    if (*p == '\0') return false;

    size_t n = 0;
    while (*p != '\0' && *p != '/') {
        if (n + 1 >= out_size) return false;
        out[n++] = *p++;
    }
    out[n] = '\0';
    return n != 0;
}

} // namespace

bool initialize(uint64_t sector_offset) {
    g_initialized = false;
    g_partition_sector = sector_offset;
    g_big_endian = false;

    if (!virtio_blk::init()) return false;

    if (read_superblock_at(SBLOCK_UFS2) ||
        read_superblock_at(SBLOCK_UFS1) ||
        read_superblock_at(SBLOCK_PIGGY)) {
        g_initialized = true;
        return true;
    }

    return false;
}

bool mount(uint64_t sector_offset) {
    return initialize(sector_offset);
}

bool is_initialized() {
    return g_initialized;
}

const SuperblockInfo* superblock() {
    return g_initialized ? &g_sb : nullptr;
}

bool read_inode(uint64_t inode_number, Inode* out) {
    if (!g_initialized || !out) return false;
    uint8_t raw[UFS2_INODE_SIZE];
    if (!read_inode_raw(inode_number, raw)) return false;
    return parse_inode(raw, out);
}

bool read_file_range(uint64_t inode_number, uint64_t offset,
                     uint8_t* dest, size_t length, size_t* out_size) {
    if (out_size) *out_size = 0;
    if (!g_initialized || (!dest && length != 0)) return false;

    Inode inode{};
    if (!read_inode(inode_number, &inode)) return false;
    if ((inode.mode & IFMT) != IFREG && (inode.mode & IFMT) != IFDIR) return false;
    if (offset > inode.size) return false;

    uint64_t available = inode.size - offset;
    if (available < length) length = static_cast<size_t>(available);

    size_t done = 0;
    while (done < length) {
        uint64_t absolute = offset + done;
        uint64_t lbn = absolute / g_sb.block_size;
        uint32_t in_block = static_cast<uint32_t>(absolute % g_sb.block_size);
        size_t n = g_sb.block_size - in_block;
        if (n > length - done) n = length - done;

        uint64_t physical = 0;
        if (!map_file_block(inode, lbn, &physical)) return false;

        if (physical == 0) {
            for (size_t i = 0; i < n; ++i) dest[done + i] = 0;
        } else {
            if (!read_fs_block(physical, g_block)) return false;
            for (size_t i = 0; i < n; ++i)
                dest[done + i] = g_block[in_block + i];
        }
        done += n;
    }

    if (out_size) *out_size = done;
    return true;
}

bool read_file(uint64_t inode_number, uint8_t* dest, size_t max_size,
               size_t* out_size) {
    if (out_size) *out_size = 0;
    Inode inode{};
    if (!read_inode(inode_number, &inode)) return false;
    if (inode.size > max_size) return false;
    return read_file_range(inode_number, 0, dest,
                           static_cast<size_t>(inode.size), out_size);
}

bool lookup_child(uint64_t directory_inode, const char* name,
                  uint64_t* out_inode) {
    if (!g_initialized || !name || !out_inode) return false;
    Inode dir{};
    if (!read_inode(directory_inode, &dir)) return false;
    return lookup_in_directory(dir, name, out_inode);
}

bool find_inode(const char* path, uint64_t* out_inode) {
    if (!g_initialized || !path || !out_inode) return false;

    size_t len = string_length(path);
    if (len == 0 || len >= MAX_PATH) return false;

    // Normalize a copy without requiring libc string functions.
    for (size_t i = 0; i <= len; ++i) g_path[i] = static_cast<uint8_t>(path[i]);

    const char* p = reinterpret_cast<const char*>(g_path);
    uint64_t current = UFS2_ROOT_INO;

    while (true) {
        char component[UFS2_MAX_NAME + 1];
        if (!next_component(p, component, sizeof(component))) break;

        if (component[0] == '.' && component[1] == '\0') continue;
        if (component[0] == '.' && component[1] == '.' && component[2] == '\0') {
            uint64_t parent = 0;
            if (!lookup_child(current, "..", &parent)) return false;
            current = parent;
            continue;
        }

        uint64_t next = 0;
        if (!lookup_child(current, component, &next)) return false;
        current = next;
    }

    *out_inode = current;
    return true;
}

bool read_path(const char* path, uint8_t* dest, size_t max_size,
               size_t* out_size) {
    uint64_t ino = 0;
    if (!find_inode(path, &ino)) return false;
    return read_file(ino, dest, max_size, out_size);
}

bool read_path_range(const char* path, uint64_t offset,
                     uint8_t* dest, size_t length, size_t* out_size) {
    uint64_t ino = 0;
    if (!find_inode(path, &ino)) return false;
    return read_file_range(ino, offset, dest, length, out_size);
}

bool is_directory(uint64_t inode_number) {
    Inode inode{};
    return read_inode(inode_number, &inode) && ((inode.mode & IFMT) == IFDIR);
}

bool is_regular_file(uint64_t inode_number) {
    Inode inode{};
    return read_inode(inode_number, &inode) && ((inode.mode & IFMT) == IFREG);
}

bool is_symlink(uint64_t inode_number) {
    Inode inode{};
    return read_inode(inode_number, &inode) && ((inode.mode & IFMT) == IFLNK);
}

bool read_symlink(uint64_t inode_number, char* out, size_t out_size) {
    if (!out || out_size == 0) return false;

    Inode inode{};
    if (!read_inode(inode_number, &inode)) return false;
    if ((inode.mode & IFMT) != IFLNK) return false;
    if (inode.size >= out_size) return false;

    size_t length = static_cast<size_t>(inode.size);
    uint32_t max_inline = UFS2_NDADDR * sizeof(uint64_t) +
                          UFS2_NIADDR * sizeof(uint64_t);

    if (length <= max_inline && length <= 120) {
        // di_db starts at offset 112. For short symlinks UFS stores the
        // pathname directly in the block-pointer area of the inode.
        uint8_t raw[UFS2_INODE_SIZE];
        if (!read_inode_raw(inode_number, raw)) return false;
        for (size_t i = 0; i < length; ++i) out[i] = static_cast<char>(raw[112 + i]);
    } else {
        size_t got = 0;
        if (!read_file_range(inode_number, 0,
                             reinterpret_cast<uint8_t*>(out), length, &got))
            return false;
        if (got != length) return false;
    }

    out[length] = '\0';
    return true;
}

} // namespace ufs2
