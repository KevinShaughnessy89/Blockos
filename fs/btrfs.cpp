#include "btrfs.hpp"

#include "vfs_blk_adapter.hpp"
#include "../kernel/allocator.hpp"

#include <cstddef>
#include <cstdint>

namespace
{
constexpr uint64_t BTRFS_SUPER_OFFSET = 65536;
constexpr uint64_t BTRFS_MAGIC = 0x4D5F536652484254ULL;
constexpr uint64_t BTRFS_ROOT_TREE_OBJECTID = 1;
constexpr uint64_t BTRFS_FS_TREE_OBJECTID = 5;
constexpr uint64_t BTRFS_FIRST_FREE_OBJECTID = 256;
constexpr uint8_t BTRFS_INODE_ITEM_KEY = 1;
constexpr uint8_t BTRFS_DIR_ITEM_KEY = 84;
constexpr uint8_t BTRFS_DIR_INDEX_KEY = 96;
constexpr uint8_t BTRFS_EXTENT_DATA_KEY = 108;
constexpr uint8_t BTRFS_ROOT_ITEM_KEY = 132;
constexpr uint8_t BTRFS_CHUNK_ITEM_KEY = 228;
constexpr uint8_t BTRFS_FT_REG_FILE = 1;
constexpr uint8_t BTRFS_FT_DIR = 2;
constexpr uint8_t BTRFS_FT_SYMLINK = 7;
constexpr uint8_t BTRFS_FILE_EXTENT_INLINE = 0;
constexpr uint8_t BTRFS_FILE_EXTENT_REG = 1;
constexpr uint64_t BTRFS_BLOCK_GROUP_TYPE_MASK = 0x00000000000000FFULL;
constexpr uint64_t BTRFS_BLOCK_GROUP_PROFILE_MASK =
    0x00000000000000F0ULL;
constexpr uint64_t BTRFS_BLOCK_GROUP_RAID0 = 0x10ULL;
constexpr uint64_t BTRFS_BLOCK_GROUP_RAID1 = 0x20ULL;
constexpr uint64_t BTRFS_BLOCK_GROUP_RAID5 = 0x40ULL;
constexpr uint64_t BTRFS_BLOCK_GROUP_RAID6 = 0x50ULL;
constexpr uint64_t BTRFS_BLOCK_GROUP_RAID10 = 0x30ULL;
constexpr uint64_t BTRFS_BLOCK_GROUP_RAID56_MASK = 0x60ULL;
constexpr uint64_t BTRFS_BLOCK_GROUP_DUP = 0x20ULL;
constexpr size_t MAX_CHUNKS = 256;
constexpr size_t MAX_SCAN_DEPTH = 16;

struct DiskKey
{
    uint64_t objectid;
    uint8_t type;
    uint64_t offset;
} __attribute__((packed));

struct Header
{
    uint8_t csum[32];
    uint8_t fsid[16];
    uint64_t bytenr;
    uint64_t flags;
    uint8_t chunk_tree_uuid[16];
    uint64_t generation;
    uint64_t owner;
    uint32_t nritems;
    uint8_t level;
} __attribute__((packed));

struct Item
{
    DiskKey key;
    uint32_t offset;
    uint32_t size;
} __attribute__((packed));

struct KeyPtr
{
    DiskKey key;
    uint64_t blockptr;
    uint64_t generation;
} __attribute__((packed));

struct Chunk
{
    uint64_t logical;
    uint64_t length;
    uint64_t physical;
    uint64_t type;
    uint64_t devid;
};

struct State
{
    bool initialized;
    bool valid;
    uint32_t sector_size;
    uint32_t node_size;
    uint64_t total_bytes;
    uint64_t bytes_used;
    uint64_t root;
    uint64_t chunk_root;
    uint8_t root_level;
    uint8_t chunk_root_level;
    uint64_t root_dir_objectid;
    uint64_t num_devices;
    uint64_t fs_tree_root;
    uint8_t fs_tree_level;
    Chunk chunks[MAX_CHUNKS];
    size_t chunk_count;
};

static State g_state = {};

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
    uint64_t v = 0;
    for (unsigned i = 0; i < 8; ++i)
        v |= static_cast<uint64_t>(p[i]) << (i * 8);
    return v;
}

static void copy_bytes(void* dst, const void* src, size_t n)
{
    uint8_t* d = static_cast<uint8_t*>(dst);
    const uint8_t* s = static_cast<const uint8_t*>(src);
    for (size_t i = 0; i < n; ++i)
        d[i] = s[i];
}

static size_t str_len(const char* s)
{
    if (!s)
        return 0;
    size_t n = 0;
    while (s[n] != '\0')
        ++n;
    return n;
}

static bool path_component(const char* path, size_t* pos,
                          char* out, size_t out_size)
{
    if (!path || !pos || !out || out_size == 0)
        return false;

    while (path[*pos] == '/')
        ++(*pos);

    if (path[*pos] == '\0')
        return false;

    size_t n = 0;
    while (path[*pos] && path[*pos] != '/')
    {
        if (n + 1 < out_size)
            out[n++] = path[*pos];
        ++(*pos);
    }
    out[n] = '\0';
    return n != 0;
}

static bool is_end(const char* path, size_t pos)
{
    if (!path)
        return true;
    while (path[pos] == '/')
        ++pos;
    return path[pos] == '\0';
}

static bool physical_read(uint64_t byte_offset, uint8_t* buffer,
                          size_t bytes)
{
    if (!buffer || bytes == 0 || g_state.sector_size == 0)
        return false;

    if ((byte_offset % g_state.sector_size) != 0 ||
        (bytes % g_state.sector_size) != 0)
        return false;

    const uint64_t sector = byte_offset / g_state.sector_size;
    const uint64_t count64 = bytes / g_state.sector_size;
    if (count64 > 0xFFFFFFFFULL)
        return false;

    if (g_state.sector_size == 512)
    {
        return vfs_blk_adapter::read_sectors_to_vfs(
            sector, static_cast<uint32_t>(count64), buffer);
    }

    /* The BlockOS block adapter is 512-byte based. */
    const uint64_t first512 = byte_offset / 512;
    const uint64_t count512 = bytes / 512;
    if (count512 > 0xFFFFFFFFULL)
        return false;
    return vfs_blk_adapter::read_sectors_to_vfs(
        first512, static_cast<uint32_t>(count512), buffer);
}

static bool add_chunk(uint64_t logical, uint64_t length,
                      uint64_t physical, uint64_t type, uint64_t devid)
{
    if (length == 0 || g_state.chunk_count >= MAX_CHUNKS)
        return false;

    Chunk& c = g_state.chunks[g_state.chunk_count++];
    c.logical = logical;
    c.length = length;
    c.physical = physical;
    c.type = type;
    c.devid = devid;
    return true;
}

static bool map_logical(uint64_t logical, uint64_t bytes,
                        uint64_t* physical)
{
    if (!physical || bytes == 0)
        return false;

    for (size_t i = 0; i < g_state.chunk_count; ++i)
    {
        const Chunk& c = g_state.chunks[i];
        if (logical < c.logical)
            continue;
        const uint64_t delta = logical - c.logical;
        if (delta >= c.length || bytes > c.length - delta)
            continue;

        const uint64_t profile = c.type & BTRFS_BLOCK_GROUP_PROFILE_MASK;
        const bool supported_profile =
            profile == 0 || profile == BTRFS_BLOCK_GROUP_DUP;

        if (!supported_profile)
            return false;
        if (c.devid != 1)
            return false;

        *physical = c.physical + delta;
        return true;
    }
    return false;
}

static bool logical_read(uint64_t logical, uint8_t* buffer, size_t bytes)
{
    if (!buffer || bytes == 0)
        return false;
    uint64_t physical = 0;
    if (!map_logical(logical, bytes, &physical))
        return false;
    return physical_read(physical, buffer, bytes);
}

static bool read_super()
{
    uint8_t sb[4096] = {};
    if (!vfs_blk_adapter::read_sectors_to_vfs(
            BTRFS_SUPER_OFFSET / 512, 8, sb))
        return false;

    if (le64(sb + 64) != BTRFS_MAGIC)
        return false;
    if (le64(sb + 48) != BTRFS_SUPER_OFFSET)
        return false;

    g_state.total_bytes = le64(sb + 112);
    g_state.bytes_used = le64(sb + 120);
    g_state.root_dir_objectid = le64(sb + 128);
    g_state.num_devices = le64(sb + 136);
    g_state.sector_size = le32(sb + 144);
    g_state.node_size = le32(sb + 148);
    g_state.root = le64(sb + 80);
    g_state.chunk_root = le64(sb + 88);
    g_state.root_level = sb[198];
    g_state.chunk_root_level = sb[199];

    if (g_state.sector_size < 512 ||
        (g_state.sector_size & (g_state.sector_size - 1)) != 0 ||
        g_state.node_size < g_state.sector_size ||
        (g_state.node_size % g_state.sector_size) != 0)
        return false;

    return true;
}

static bool parse_sys_chunks(const uint8_t* sb)
{
    /* sys_chunk_array follows the fixed superblock fields. */
    const size_t sys_array_offset = 0x32B;
    const uint32_t array_size = le32(sb + 160);
    if (array_size == 0 || array_size > 2048)
        return false;

    size_t p = sys_array_offset;
    const size_t end = p + array_size;

    while (p + 17 <= end && g_state.chunk_count < MAX_CHUNKS)
    {
        const uint64_t logical = le64(sb + p);
        const uint8_t type = sb[p + 8];
        const uint64_t length = le64(sb + p + 9);
        p += 17;

        if (p + 48 > end || length == 0)
            break;

        /* Chunk item fixed header: 48 bytes, then stripes. */
        const uint32_t io_align = le32(sb + p + 32);
        (void)io_align;
        const uint16_t num_stripes = le16(sb + p + 44);
        if (num_stripes == 0)
            break;

        const uint64_t devid = le64(sb + p + 48);
        const uint64_t physical = le64(sb + p + 56);
        if (!add_chunk(logical, length, physical, type, devid))
            return false;

        const size_t chunk_size = 48 + static_cast<size_t>(num_stripes) * 32;
        if (chunk_size < 48 || p + chunk_size > end)
            break;
        p += chunk_size;
    }

    return g_state.chunk_count != 0;
}

using LeafCallback = bool (*)(const uint8_t* leaf, uint32_t nritems,
                              void* context);

static bool walk_tree(uint64_t bytenr, uint8_t level,
                      LeafCallback callback, void* context,
                      unsigned depth = 0)
{
    if (!callback || depth > MAX_SCAN_DEPTH)
        return false;

    uint8_t* block = static_cast<uint8_t*>(
        allocator::alloc(g_state.node_size, g_state.sector_size));
    if (!block)
        return false;

    const bool ok = logical_read(bytenr, block, g_state.node_size);
    if (!ok)
    {
        allocator::free(block);
        return false;
    }

    const Header* h = reinterpret_cast<const Header*>(block);
    if (h->bytenr != bytenr || h->level != level ||
        h->nritems > (g_state.node_size - sizeof(Header)) /
                         (level == 0 ? sizeof(Item) : sizeof(KeyPtr)))
    {
        allocator::free(block);
        return false;
    }

    const uint32_t n = h->nritems;
    if (level == 0)
    {
        const bool result = callback(block, n, context);
        allocator::free(block);
        return result;
    }

    const KeyPtr* ptrs = reinterpret_cast<const KeyPtr*>(
        block + sizeof(Header));
    for (uint32_t i = 0; i < n; ++i)
    {
        if (!walk_tree(ptrs[i].blockptr, static_cast<uint8_t>(level - 1),
                       callback, context, depth + 1))
        {
            allocator::free(block);
            return false;
        }
    }

    allocator::free(block);
    return true;
}

struct ChunkScanContext
{
    bool found;
};

static bool scan_chunk_leaf(const uint8_t* leaf, uint32_t nritems,
                            void* context)
{
    ChunkScanContext* ctx = static_cast<ChunkScanContext*>(context);
    const Item* items = reinterpret_cast<const Item*>(
        leaf + sizeof(Header));

    for (uint32_t i = 0; i < nritems; ++i)
    {
        if (items[i].key.type != BTRFS_CHUNK_ITEM_KEY)
            continue;
        if (items[i].size < 80)
            continue;

        const uint8_t* data = leaf + items[i].offset;
        const uint64_t length = le64(data + 0);
        const uint64_t type = le64(data + 24);
        const uint16_t num_stripes = le16(data + 44);
        if (length == 0 || num_stripes == 0 || items[i].size < 80)
            continue;

        const uint64_t devid = le64(data + 48);
        const uint64_t physical = le64(data + 56);
        if (!add_chunk(items[i].key.offset, length, physical, type, devid))
            return false;
        ctx->found = true;
    }
    return true;
}

struct RootScanContext
{
    bool found;
    uint64_t root;
    uint8_t level;
};

static bool scan_root_leaf(const uint8_t* leaf, uint32_t nritems,
                           void* context)
{
    RootScanContext* ctx = static_cast<RootScanContext*>(context);
    const Item* items = reinterpret_cast<const Item*>(
        leaf + sizeof(Header));

    for (uint32_t i = 0; i < nritems; ++i)
    {
        if (items[i].key.objectid != BTRFS_FS_TREE_OBJECTID ||
            items[i].key.type != BTRFS_ROOT_ITEM_KEY)
            continue;
        if (items[i].size < 439)
            continue;

        const uint8_t* data = leaf + items[i].offset;
        ctx->root = le64(data + 184);
        ctx->level = data[438];
        ctx->found = ctx->root != 0;
        return true;
    }
    return true;
}

struct InodeContext
{
    uint64_t inode;
    bool found;
    uint64_t size;
    uint32_t mode;
};

static bool scan_inode_leaf(const uint8_t* leaf, uint32_t nritems,
                            void* context)
{
    InodeContext* ctx = static_cast<InodeContext*>(context);
    const Item* items = reinterpret_cast<const Item*>(
        leaf + sizeof(Header));
    for (uint32_t i = 0; i < nritems; ++i)
    {
        if (items[i].key.objectid != ctx->inode ||
            items[i].key.type != BTRFS_INODE_ITEM_KEY ||
            items[i].key.offset != 0 || items[i].size < 160)
            continue;

        const uint8_t* data = leaf + items[i].offset;
        ctx->size = le64(data + 16);
        ctx->mode = le32(data + 88);
        ctx->found = true;
        return true;
    }
    return true;
}

struct DirFindContext
{
    uint64_t dir_inode;
    const char* wanted;
    uint64_t inode;
    uint8_t type;
    bool found;
};

static bool name_equals(const uint8_t* name, size_t name_len,
                        const char* wanted)
{
    const size_t wanted_len = str_len(wanted);
    if (name_len != wanted_len)
        return false;
    for (size_t i = 0; i < name_len; ++i)
    {
        char a = static_cast<char>(name[i]);
        char b = wanted[i];
        if (a >= 'A' && a <= 'Z')
            a = static_cast<char>(a - 'A' + 'a');
        if (b >= 'A' && b <= 'Z')
            b = static_cast<char>(b - 'A' + 'a');
        if (a != b)
            return false;
    }
    return true;
}

static bool scan_dir_find_leaf(const uint8_t* leaf, uint32_t nritems,
                               void* context)
{
    DirFindContext* ctx = static_cast<DirFindContext*>(context);
    const Item* items = reinterpret_cast<const Item*>(
        leaf + sizeof(Header));

    for (uint32_t i = 0; i < nritems; ++i)
    {
        if (items[i].key.objectid != ctx->dir_inode ||
            items[i].key.type != BTRFS_DIR_ITEM_KEY)
            continue;

        const uint8_t* data = leaf + items[i].offset;
        const size_t remaining = items[i].size;
        if (remaining < 30)
            continue;

        const uint16_t data_len = le16(data + 25);
        const uint16_t name_len = le16(data + 27);
        if (30ULL + data_len + name_len > remaining)
            continue;

        const uint8_t* location = data;
        const uint8_t type = data[29];
        if (name_equals(data + 30, name_len, ctx->wanted))
        {
            ctx->inode = le64(location + 0);
            ctx->type = type;
            ctx->found = true;
            return true;
        }
    }
    return true;
}

struct ListContext
{
    uint64_t dir_inode;
    btrfs::ListCallback callback;
    void* user;
    bool failed;
};

static bool scan_dir_list_leaf(const uint8_t* leaf, uint32_t nritems,
                               void* context)
{
    ListContext* ctx = static_cast<ListContext*>(context);
    const Item* items = reinterpret_cast<const Item*>(
        leaf + sizeof(Header));

    for (uint32_t i = 0; i < nritems; ++i)
    {
        if (items[i].key.objectid != ctx->dir_inode ||
            items[i].key.type != BTRFS_DIR_ITEM_KEY)
            continue;

        const uint8_t* data = leaf + items[i].offset;
        if (items[i].size < 30)
            continue;

        const uint16_t data_len = le16(data + 25);
        const uint16_t name_len = le16(data + 27);
        if (30ULL + data_len + name_len > items[i].size || name_len == 0)
            continue;

        btrfs::FileInfo info = {};
        const size_t copy_len = name_len < sizeof(info.name) - 1
                                    ? name_len
                                    : sizeof(info.name) - 1;
        copy_bytes(info.name, data + 30, copy_len);
        info.name[copy_len] = '\0';
        info.inode = le64(data + 0);
        info.directory = data[29] == BTRFS_FT_DIR;
        info.regular = data[29] == BTRFS_FT_REG_FILE;

        InodeContext inode_ctx = {};
        inode_ctx.inode = info.inode;
        if (!walk_tree(g_state.fs_tree_root, g_state.fs_tree_level,
                       scan_inode_leaf, &inode_ctx))
        {
            ctx->failed = true;
            return false;
        }
        if (inode_ctx.found)
        {
            info.size = inode_ctx.size;
            info.mode = inode_ctx.mode;
        }

        if (ctx->callback && !ctx->callback(info, ctx->user))
            return false;
    }
    return true;
}

struct FileReadContext
{
    uint64_t inode;
    uint8_t* dest;
    uint64_t wanted_start;
    uint64_t wanted_end;
    uint64_t copied;
    bool unsupported;
};

static bool scan_extent_leaf(const uint8_t* leaf, uint32_t nritems,
                             void* context)
{
    FileReadContext* ctx = static_cast<FileReadContext*>(context);
    const Item* items = reinterpret_cast<const Item*>(
        leaf + sizeof(Header));

    for (uint32_t i = 0; i < nritems; ++i)
    {
        if (items[i].key.objectid != ctx->inode ||
            items[i].key.type != BTRFS_EXTENT_DATA_KEY ||
            items[i].size < 53)
            continue;

        const uint64_t extent_file_offset = items[i].key.offset;
        const uint8_t* data = leaf + items[i].offset;
        const uint8_t compression = data[16 + 16 + 0];
        const uint8_t encryption = data[16 + 16 + 1];
        const uint8_t extent_type = data[16 + 16 + 4];

        if (compression != 0 || encryption != 0)
        {
            ctx->unsupported = true;
            continue;
        }

        uint64_t extent_len = 0;
        if (extent_type == BTRFS_FILE_EXTENT_INLINE)
        {
            extent_len = items[i].size - 53;
            const uint64_t extent_end = extent_file_offset + extent_len;
            if (extent_end <= ctx->wanted_start ||
                extent_file_offset >= ctx->wanted_end)
                continue;

            uint64_t from = ctx->wanted_start > extent_file_offset
                                ? ctx->wanted_start
                                : extent_file_offset;
            uint64_t to = ctx->wanted_end < extent_end
                              ? ctx->wanted_end
                              : extent_end;
            const size_t n = static_cast<size_t>(to - from);
            const size_t src_off = 53 +
                static_cast<size_t>(from - extent_file_offset);
            copy_bytes(ctx->dest + (from - ctx->wanted_start),
                       data + src_off, n);
            ctx->copied += n;
            continue;
        }

        if (extent_type != BTRFS_FILE_EXTENT_REG)
            continue;

        const uint64_t disk_bytenr = le64(data + 21);
        const uint64_t disk_num_bytes = le64(data + 29);
        const uint64_t file_offset = le64(data + 37);
        const uint64_t num_bytes = le64(data + 45);
        if (num_bytes == 0 || disk_num_bytes == 0)
            continue;

        extent_len = num_bytes;
        const uint64_t extent_end = extent_file_offset + extent_len;
        if (extent_end <= ctx->wanted_start ||
            extent_file_offset >= ctx->wanted_end)
            continue;

        uint64_t from = ctx->wanted_start > extent_file_offset
                            ? ctx->wanted_start
                            : extent_file_offset;
        uint64_t to = ctx->wanted_end < extent_end
                          ? ctx->wanted_end
                          : extent_end;
        const size_t n = static_cast<size_t>(to - from);
        const uint64_t inside = from - extent_file_offset;
        const uint64_t logical_disk = disk_bytenr +
                                      (inside + file_offset);

        if ((logical_disk % g_state.sector_size) != 0 ||
            (n % g_state.sector_size) != 0)
        {
            /* Unaligned file reads are handled below in smaller pieces. */
            size_t done = 0;
            uint8_t sector[4096] = {};
            while (done < n)
            {
                const uint64_t pos = logical_disk + done;
                const uint64_t sector_base =
                    pos - (pos % g_state.sector_size);
                uint64_t physical = 0;
                if (!map_logical(sector_base, g_state.sector_size,
                                 &physical))
                    return false;
                if (!physical_read(sector_base, sector,
                                   g_state.sector_size))
                    return false;
                const size_t in_sector = static_cast<size_t>(
                    pos - sector_base);
                size_t take = g_state.sector_size - in_sector;
                if (take > n - done)
                    take = n - done;
                copy_bytes(ctx->dest + (from - ctx->wanted_start) + done,
                           sector + in_sector, take);
                done += take;
            }
        }
        else if (!logical_read(logical_disk,
                               ctx->dest + (from - ctx->wanted_start), n))
        {
            return false;
        }
        ctx->copied += n;
    }
    return true;
}

static bool inode_info(uint64_t inode, uint64_t* size, uint32_t* mode)
{
    InodeContext ctx = {};
    ctx.inode = inode;
    if (!walk_tree(g_state.fs_tree_root, g_state.fs_tree_level,
                   scan_inode_leaf, &ctx) || !ctx.found)
        return false;
    if (size)
        *size = ctx.size;
    if (mode)
        *mode = ctx.mode;
    return true;
}

static bool find_child(uint64_t dir_inode, const char* name,
                       uint64_t* inode, uint8_t* type)
{
    DirFindContext ctx = {};
    ctx.dir_inode = dir_inode;
    ctx.wanted = name;
    if (!walk_tree(g_state.fs_tree_root, g_state.fs_tree_level,
                   scan_dir_find_leaf, &ctx))
        return false;
    if (!ctx.found)
        return false;
    if (inode)
        *inode = ctx.inode;
    if (type)
        *type = ctx.type;
    return true;
}

static bool resolve_path(const char* path, uint64_t* inode,
                         uint8_t* file_type)
{
    if (!path || !inode)
        return false;

    size_t pos = 0;
    while (path[pos] == '/')
        ++pos;

    uint64_t current = g_state.root_dir_objectid;
    uint8_t current_type = BTRFS_FT_DIR;

    if (path[pos] == '\0')
    {
        *inode = current;
        if (file_type)
            *file_type = current_type;
        return true;
    }

    char component[256];
    while (path_component(path, &pos, component, sizeof(component)))
    {
        if (!find_child(current, component, &current, &current_type))
            return false;

        if (!is_end(path, pos) && current_type != BTRFS_FT_DIR)
            return false;
    }

    *inode = current;
    if (file_type)
        *file_type = current_type;
    return true;
}

} // namespace

namespace btrfs
{

bool init()
{
    g_state = {};
    if (!vfs_blk_adapter::init_backend())
        return false;

    if (!read_super())
        return false;

    uint8_t sb[4096] = {};
    if (!vfs_blk_adapter::read_sectors_to_vfs(
            BTRFS_SUPER_OFFSET / 512, 8, sb))
        return false;

    if (!parse_sys_chunks(sb))
        return false;

    ChunkScanContext chunk_ctx = {};
    if (!walk_tree(g_state.chunk_root, g_state.chunk_root_level,
                   scan_chunk_leaf, &chunk_ctx))
        return false;

    RootScanContext root_ctx = {};
    if (!walk_tree(g_state.root, g_state.root_level,
                   scan_root_leaf, &root_ctx))
        return false;
    if (!root_ctx.found)
        return false;

    g_state.fs_tree_root = root_ctx.root;
    g_state.fs_tree_level = root_ctx.level;
    g_state.initialized = true;
    g_state.valid = true;
    return true;
}

bool probe()
{
    return init();
}

bool mounted()
{
    return g_state.initialized && g_state.valid;
}

bool find(const char* path, FileInfo* out)
{
    if (!mounted() || !path || !out)
        return false;

    uint64_t inode = 0;
    uint8_t type = 0;
    if (!resolve_path(path, &inode, &type))
        return false;

    *out = {};
    out->inode = inode;
    out->directory = type == BTRFS_FT_DIR;
    out->regular = type == BTRFS_FT_REG_FILE;
    if (!inode_info(inode, &out->size, &out->mode))
        return false;

    return true;
}

bool read_file(const char* path, uint64_t offset, uint8_t* dest,
               size_t size, size_t* out_read)
{
    if (out_read)
        *out_read = 0;
    if (!mounted() || !path || !dest || size == 0)
        return false;

    FileInfo info = {};
    if (!find(path, &info) || !info.regular)
        return false;
    if (offset >= info.size)
        return true;

    uint64_t wanted = size;
    if (wanted > info.size - offset)
        wanted = info.size - offset;

    FileReadContext ctx = {};
    ctx.inode = info.inode;
    ctx.dest = dest;
    ctx.wanted_start = offset;
    ctx.wanted_end = offset + wanted;

    if (!walk_tree(g_state.fs_tree_root, g_state.fs_tree_level,
                   scan_extent_leaf, &ctx))
        return false;
    if (ctx.unsupported)
        return false;

    if (out_read)
        *out_read = static_cast<size_t>(ctx.copied);
    return ctx.copied == wanted;
}

bool list_directory(const char* path, ListCallback callback, void* context)
{
    if (!mounted() || !path || !callback)
        return false;

    FileInfo info = {};
    if (!find(path, &info) || !info.directory)
        return false;

    ListContext ctx = {};
    ctx.dir_inode = info.inode;
    ctx.callback = callback;
    ctx.user = context;

    const bool ok = walk_tree(g_state.fs_tree_root, g_state.fs_tree_level,
                              scan_dir_list_leaf, &ctx);
    return ok && !ctx.failed;
}

uint64_t total_bytes()
{
    return g_state.total_bytes;
}

uint64_t used_bytes()
{
    return g_state.bytes_used;
}

uint32_t sector_size()
{
    return g_state.sector_size;
}

uint32_t node_size()
{
    return g_state.node_size;
}

} // namespace btrfs
