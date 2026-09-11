#include "jfs.hpp"
#include "../drivers/virtio_blk.hpp"

#include <stdint.h>
#include <stddef.h>

namespace jfs {

static uint16_t le16(const uint8_t* p) {
    return (uint16_t)p[0] | ((uint16_t)p[1] << 8);
}
static uint32_t le32(const uint8_t* p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}
static uint64_t le64(const uint8_t* p) {
    return (uint64_t)le32(p) | ((uint64_t)le32(p + 4) << 32);
}
static void copy_bytes(uint8_t* d, const uint8_t* s, size_t n) {
    for (size_t i = 0; i < n; ++i) d[i] = s[i];
}
static void zero_bytes(uint8_t* p, size_t n) {
    for (size_t i = 0; i < n; ++i) p[i] = 0;
}

Reader filesystem;

Reader::Reader() : disk_id_(0), initialized_(false), mounted_(false) {
    zero_bytes(reinterpret_cast<uint8_t*>(&sb_), sizeof(sb_));
}

uint32_t Reader::pxd_length(const Pxd& pxd) const {
    return pxd.len_addr & 0x00ffffffU;
}

uint64_t Reader::pxd_address(const Pxd& pxd) const {
    uint64_t high = (uint64_t)(pxd.len_addr & 0xff000000U) >> 24;
    return (high << 32) | pxd.addr2;
}

bool Reader::initialize(uint32_t disk_id) {
    disk_id_ = disk_id;
    initialized_ = virtio_blk::init();
    mounted_ = false;
    return initialized_;
}

bool Reader::read_bytes(uint64_t byte_offset, uint8_t* buffer, size_t bytes) {
    if (!initialized_ || !buffer) return false;
    if (bytes == 0) return true;

    uint64_t first_sector = byte_offset / 512ULL;
    uint64_t last_sector = (byte_offset + bytes - 1) / 512ULL;
    uint8_t sector[512];
    size_t done = 0;

    for (uint64_t s = first_sector; s <= last_sector; ++s) {
        if (!virtio_blk::read_sector(s, sector)) return false;
        uint64_t sector_start = s * 512ULL;
        size_t from = byte_offset > sector_start ? (size_t)(byte_offset - sector_start) : 0;
        size_t take = 512 - from;
        if (take > bytes - done) take = bytes - done;
        copy_bytes(buffer + done, sector + from, take);
        done += take;
        if (done == bytes) break;
    }
    return done == bytes;
}

bool Reader::read_superblock_copy(uint64_t byte_offset, JfsSuperblock* out) {
    if (!out) return false;
    uint8_t raw[512];
    if (!read_bytes(byte_offset, raw, sizeof(raw))) return false;

    copy_bytes((uint8_t*)out->magic, raw + 0x00, 4);
    out->version = le32(raw + 0x04);
    out->size = le64(raw + 0x08);
    out->block_size = le32(raw + 0x10);
    out->log2_block_size = le16(raw + 0x14);
    out->log2_block_factor = le16(raw + 0x16);
    out->physical_block_size = le32(raw + 0x18);
    out->log2_physical_block_size = le16(raw + 0x1c);
    out->reserved0 = le16(raw + 0x1e);
    out->ag_size = le32(raw + 0x20);
    out->flags = le32(raw + 0x24);
    out->state = le32(raw + 0x28);
    out->compression = le32(raw + 0x2c);

    out->ait2.len_addr = le32(raw + 0x30);
    out->ait2.addr2 = le32(raw + 0x34);
    out->aim2.len_addr = le32(raw + 0x38);
    out->aim2.addr2 = le32(raw + 0x3c);
    out->log_device = le32(raw + 0x40);
    out->log_serial = le32(raw + 0x44);
    out->log_pxd.len_addr = le32(raw + 0x48);
    out->log_pxd.addr2 = le32(raw + 0x4c);
    out->fsck_pxd.len_addr = le32(raw + 0x50);
    out->fsck_pxd.addr2 = le32(raw + 0x54);
    out->time_sec = le32(raw + 0x58);
    out->time_nsec = le32(raw + 0x5c);
    out->fsck_log_length = le32(raw + 0x60);
    out->fsck_log = (int8_t)raw[0x64];
    copy_bytes((uint8_t*)out->volume_name, raw + 0x65, 11);
    out->extend_size = le64(raw + 0x70);
    out->extend_fsck.len_addr = le32(raw + 0x78);
    out->extend_fsck.addr2 = le32(raw + 0x7c);
    out->extend_log.len_addr = le32(raw + 0x80);
    out->extend_log.addr2 = le32(raw + 0x84);
    copy_bytes(out->uuid, raw + 0x88, 16);
    copy_bytes((uint8_t*)out->label, raw + 0x98, 16);
    copy_bytes(out->log_uuid, raw + 0xa8, 16);
    return true;
}

bool Reader::validate_superblock() const {
    if (sb_.magic[0] != 'J' || sb_.magic[1] != 'F' ||
        sb_.magic[2] != 'S' || sb_.magic[3] != '1') return false;
    if (sb_.version != 1 && sb_.version != JFS_VERSION) return false;
    if (sb_.block_size < JFS_MIN_BLOCK_SIZE ||
        sb_.block_size > JFS_MAX_BLOCK_SIZE) return false;
    if ((sb_.block_size & (sb_.block_size - 1)) != 0) return false;
    if (sb_.physical_block_size != 512) return false;
    if (sb_.size == 0) return false;
    return true;
}

bool Reader::mount() {
    if (!initialized_ && !initialize(disk_id_)) return false;

    // JFS primary superblock is at byte offset 32 KiB (64 x 512-byte sectors).
    JfsSuperblock primary;
    if (!read_superblock_copy(64ULL * 512ULL, &primary)) return false;

    sb_ = primary;
    if (!validate_superblock()) {
        // The secondary copy is at 46 KiB (92 x 512-byte sectors) on the
        // classic JFS layout. Try it as a recovery/fallback copy.
        JfsSuperblock secondary;
        if (!read_superblock_copy(92ULL * 512ULL, &secondary)) return false;
        sb_ = secondary;
        if (!validate_superblock()) return false;
    }

    mounted_ = true;
    return true;
}

bool Reader::is_mounted() const { return mounted_; }
uint32_t Reader::block_size() const { return sb_.block_size; }
uint64_t Reader::block_count() const { return sb_.size; }
const JfsSuperblock& Reader::superblock() const { return sb_; }
bool Reader::is_clean() const { return sb_.state == 0; }
bool Reader::has_inline_log() const { return (sb_.flags & 0x00000800U) != 0; }
bool Reader::has_compression() const { return sb_.compression != 0; }

bool Reader::read_block(uint64_t block, uint8_t* buffer) {
    if (!mounted_ || !buffer || block >= sb_.size) return false;
    return read_bytes(block * (uint64_t)sb_.block_size, buffer, sb_.block_size);
}

bool Reader::read_blocks(uint64_t block, uint32_t count, uint8_t* buffer) {
    if (!mounted_ || !buffer) return false;
    for (uint32_t i = 0; i < count; ++i) {
        if (!read_block(block + i, buffer + (size_t)i * sb_.block_size)) return false;
    }
    return true;
}

static void parse_inode(const uint8_t* raw, JfsInode* out) {
    out->inode_stamp = le32(raw + 0);
    out->fileset = le32(raw + 4);
    out->number = le32(raw + 8);
    out->generation = le32(raw + 12);
    out->inode_extent.len_addr = le32(raw + 16);
    out->inode_extent.addr2 = le32(raw + 20);
    out->size = le64(raw + 24);
    out->blocks = le64(raw + 32);
    out->links = le32(raw + 40);
    out->uid = le32(raw + 44);
    out->gid = le32(raw + 48);
    out->mode = le32(raw + 52);
    out->atime_sec = le32(raw + 56);
    out->atime_nsec = le32(raw + 60);
    out->ctime_sec = le32(raw + 64);
    out->ctime_nsec = le32(raw + 68);
    out->mtime_sec = le32(raw + 72);
    out->mtime_nsec = le32(raw + 76);
    out->otime_sec = le32(raw + 80);
    out->otime_nsec = le32(raw + 84);
    copy_bytes(out->acl, raw + 88, 16);
    copy_bytes(out->ea, raw + 104, 16);
    out->next_index = le32(raw + 120);
    out->acl_type = le32(raw + 124);
    copy_bytes(out->extension, raw + 128, 384);
}

bool Reader::read_inode_from_ait(uint32_t inode_number, JfsInode* out) {
    if (!out || !mounted_ || inode_number > 0x7fffffffU) return false;

    // Primary AIT starts at physical block 88 in the classic JFS layout.
    // This function intentionally handles aggregate inode numbers, which are
    // useful for inspecting the JFS metadata tree without a kernel VFS.
    const uint64_t ait_start = 88;
    const uint64_t byte_offset = ait_start * 512ULL + (uint64_t)inode_number * JFS_INODE_SIZE;

    uint8_t raw[JFS_INODE_SIZE];
    if (!read_bytes(byte_offset, raw, sizeof(raw))) return false;
    parse_inode(raw, out);
    return out->number == inode_number;
}

bool Reader::read_aggregate_inode(uint32_t inode_number, JfsInode* out) {
    return read_inode_from_ait(inode_number, out);
}

bool Reader::read_inode_extent(const Pxd& pxd, uint8_t* out, size_t bytes) {
    if (!mounted_ || !out || bytes == 0) return false;
    uint64_t start = pxd_address(pxd);
    uint64_t available = (uint64_t)pxd_length(pxd) * sb_.block_size;
    if ((uint64_t)bytes > available) return false;
    return read_bytes(start * sb_.block_size, out, bytes);
}

bool Reader::read_xtree_page(uint64_t block, uint8_t* page) {
    if (!page || sb_.block_size < 4096) return false;
    return read_block(block, page);
}

static uint64_t xad_offset(const uint8_t* x) {
    return ((uint64_t)x[3] << 32) | le32(x + 4);
}
static uint64_t pxd_addr_raw(const uint8_t* p) {
    uint64_t high = (uint64_t)(le32(p) & 0xff000000U) >> 24;
    return (high << 32) | le32(p + 4);
}
static uint32_t pxd_len_raw(const uint8_t* p) {
    return le32(p) & 0x00ffffffU;
}

bool Reader::find_extent(const uint8_t* root, uint64_t file_block, Extent* out) {
    if (!root || !out) return false;

    // An xtree root begins with the 24-byte xtheader, followed by XADs.
    // Entries start at slot 2 in the on-disk JFS tree representation.
    const uint8_t* header = root;
    uint16_t next_index = le16(header + 16);
    uint16_t max_entry = le16(header + 18);
    if (next_index < 2 || max_entry < next_index || max_entry > 256) return false;

    uint32_t slot = 2;
    uint64_t chosen = 0;
    bool found = false;
    for (; slot < next_index; ++slot) {
        const uint8_t* x = root + slot * 16;
        uint64_t off = xad_offset(x);
        uint32_t len = pxd_len_raw(x + 8);
        if (len == 0) continue;
        if (file_block < off) break;
        chosen = slot;
        found = file_block < off + len;
        if (found) {
            out->flags = x[0];
            out->file_block = off;
            out->disk_block = pxd_addr_raw(x + 8) + (file_block - off);
            out->length = len - (uint32_t)(file_block - off);
            return true;
        }
    }
    (void)chosen;
    return false;
}

bool Reader::read_extent_data(const uint8_t* extent_root,
                              uint64_t file_block,
                              uint8_t* out_block) {
    Extent e;
    if (!find_extent(extent_root, file_block, &e)) return false;
    return read_block(e.disk_block, out_block);
}

bool Reader::read_inode_data_block(const JfsInode& inode,
                                   uint64_t file_block,
                                   uint8_t* out_block) {
    // The extension area of a regular file inode contains the 288-byte
    // xtree root. The base inode occupies 128 bytes.
    return read_extent_data(inode.extension + 96, file_block, out_block);
}

bool Reader::read_inode_bytes(const JfsInode& inode,
                              uint64_t offset,
                              uint8_t* out,
                              size_t bytes) {
    if (!mounted_ || !out) return false;
    if (offset > inode.size || (uint64_t)bytes > inode.size - offset) return false;
    if (bytes == 0) return true;

    uint8_t* block = new uint8_t[sb_.block_size];
    if (!block) return false;

    size_t done = 0;
    while (done < bytes) {
        uint64_t pos = offset + done;
        uint64_t file_block = pos / sb_.block_size;
        size_t in_block = (size_t)(pos % sb_.block_size);
        size_t take = sb_.block_size - in_block;
        if (take > bytes - done) take = bytes - done;

        if (!read_inode_data_block(inode, file_block, block)) {
            delete[] block;
            return false;
        }
        copy_bytes(out + done, block + in_block, take);
        done += take;
    }

    delete[] block;
    return true;
}

bool Reader::read_file_range(uint32_t inode_number,
                             uint64_t offset,
                             uint8_t* out,
                             size_t bytes) {
    JfsInode inode;
    if (!read_aggregate_inode(inode_number, &inode)) return false;
    return read_inode_bytes(inode, offset, out, bytes);
}

bool Reader::read_file(uint32_t inode_number,
                       uint8_t* out,
                       size_t max_size,
                       size_t* bytes_read) {
    if (bytes_read) *bytes_read = 0;
    JfsInode inode;
    if (!read_aggregate_inode(inode_number, &inode)) return false;
    size_t wanted = inode.size > (uint64_t)max_size ? max_size : (size_t)inode.size;
    if (!read_inode_bytes(inode, 0, out, wanted)) return false;
    if (bytes_read) *bytes_read = wanted;
    return true;
}

bool initialize(uint32_t disk_id) { return filesystem.initialize(disk_id); }
bool mount() { return filesystem.mount(); }
bool is_mounted() { return filesystem.is_mounted(); }
bool read_file(uint32_t inode_number, uint8_t* out, size_t max_size, size_t* bytes_read) {
    return filesystem.read_file(inode_number, out, max_size, bytes_read);
}
bool read_file_range(uint32_t inode_number, uint64_t offset, uint8_t* out, size_t bytes) {
    return filesystem.read_file_range(inode_number, offset, out, bytes);
}

} // namespace jfs
