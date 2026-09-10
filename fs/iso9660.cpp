#include "iso9660.hpp"
#include "vfs_blk_adapter.hpp"

#include <stdint.h>
#include <stddef.h>

namespace iso9660
{
namespace
{
    static constexpr uint64_t PHYSICAL_SECTOR_SIZE = 512;
    static constexpr uint32_t MAX_LOGICAL_BLOCK_SIZE = 4096;
    static constexpr uint32_t ISO_DESCRIPTOR_START = 16;
    static constexpr uint8_t DESCRIPTOR_PRIMARY = 1;
    static constexpr uint8_t DESCRIPTOR_SUPPLEMENTARY = 2;
    static constexpr uint8_t DESCRIPTOR_TERMINATOR = 255;

    static uint16_t read_le16(const uint8_t* p)
    {
        return static_cast<uint16_t>(
            static_cast<uint16_t>(p[0]) |
            (static_cast<uint16_t>(p[1]) << 8));
    }

    static uint32_t read_le32(const uint8_t* p)
    {
        return
            static_cast<uint32_t>(p[0]) |
            (static_cast<uint32_t>(p[1]) << 8) |
            (static_cast<uint32_t>(p[2]) << 16) |
            (static_cast<uint32_t>(p[3]) << 24);
    }

    static uint16_t read_be16(const uint8_t* p)
    {
        return static_cast<uint16_t>(
            (static_cast<uint16_t>(p[0]) << 8) |
            static_cast<uint16_t>(p[1]));
    }

    static uint32_t read_be32(const uint8_t* p)
    {
        return
            (static_cast<uint32_t>(p[0]) << 24) |
            (static_cast<uint32_t>(p[1]) << 16) |
            (static_cast<uint32_t>(p[2]) << 8) |
            static_cast<uint32_t>(p[3]);
    }

    static uint64_t ceil_div_u64(uint64_t value, uint64_t divisor)
    {
        if (divisor == 0)
            return 0;

        return (value + divisor - 1) / divisor;
    }

    static size_t string_length(const char* str)
    {
        if (!str)
            return 0;

        size_t length = 0;

        while (str[length] != '\0')
            ++length;

        return length;
    }

    static bool is_ascii_digit(char c)
    {
        return c >= '0' && c <= '9';
    }

    static char ascii_lower(char c)
    {
        if (c >= 'A' && c <= 'Z')
            return static_cast<char>(c - 'A' + 'a');

        return c;
    }

    static bool strings_equal_case_insensitive_ascii(
        const char* a,
        const char* b)
    {
        if (!a || !b)
            return false;

        size_t i = 0;

        while (a[i] != '\0' || b[i] != '\0')
        {
            if (ascii_lower(a[i]) != ascii_lower(b[i]))
                return false;

            ++i;
        }

        return true;
    }

    static void clear_entry(ISO9660DirectoryEntry& entry)
    {
        for (size_t i = 0; i < sizeof(entry.name); ++i)
            entry.name[i] = '\0';

        entry.extent_lba = 0;
        entry.size = 0;
        entry.flags = 0;
        entry.file_unit_size = 0;
        entry.interleave_gap_size = 0;
        entry.volume_sequence = 0;
        entry.directory = false;
        entry.hidden = false;
        entry.associated = false;
        entry.multi_extent = false;
        entry.rock_ridge = false;
    }

    static bool signature_is_cd001(const uint8_t* buffer)
    {
        return
            buffer != nullptr &&
            buffer[1] == 'C' &&
            buffer[2] == 'D' &&
            buffer[3] == '0' &&
            buffer[4] == '0' &&
            buffer[5] == '1';
    }

    static int joliet_level(const uint8_t* escape)
    {
        if (!escape)
            return 0;

        if (escape[0] != '%' || escape[1] != '/')
            return 0;

        if (escape[2] == 'E')
            return 3;

        if (escape[2] == 'C')
            return 2;

        if (escape[2] == '@')
            return 1;

        return 0;
    }
}

ISO9660Reader::ISO9660Reader()
    : mounted_(false),
      joliet_(false),
      rock_ridge_(false),
      logical_block_size_(0),
      volume_space_size_(0),
      root_extent_lba_(0),
      root_data_length_(0),
      root_flags_(0),
      descriptor_type_(0)
{
    for (size_t i = 0; i < sizeof(volume_identifier_); ++i)
        volume_identifier_[i] = '\0';
}

bool ISO9660Reader::read_logical_block(
    uint64_t lba,
    uint8_t* buffer) const
{
    if (!buffer)
        return false;

    if (logical_block_size_ < PHYSICAL_SECTOR_SIZE)
        return false;

    if ((logical_block_size_ % PHYSICAL_SECTOR_SIZE) != 0)
        return false;

    if (logical_block_size_ > MAX_LOGICAL_BLOCK_SIZE)
        return false;

    uint64_t sectors_per_block =
        logical_block_size_ / PHYSICAL_SECTOR_SIZE;

    if (sectors_per_block == 0 || sectors_per_block > 0xFFFFFFFFULL)
        return false;

    if (lba > (0xFFFFFFFFFFFFFFFFULL / sectors_per_block))
        return false;

    uint64_t first_sector = lba * sectors_per_block;

    return vfs_blk_adapter::read_sectors_to_vfs(
        first_sector,
        static_cast<uint32_t>(sectors_per_block),
        buffer);
}

bool ISO9660Reader::read_volume_descriptor(
    uint64_t descriptor_lba,
    uint8_t* buffer)
{
    return read_logical_block(descriptor_lba, buffer);
}

bool ISO9660Reader::parse_descriptor(
    const uint8_t* buffer,
    uint8_t type,
    bool& valid,
    bool& joliet_candidate)
{
    valid = false;
    joliet_candidate = false;

    if (!buffer)
        return false;

    if (!signature_is_cd001(buffer))
        return false;

    if (buffer[6] != 1)
        return false;

    if (buffer[0] != type)
        return false;

    if (type == DESCRIPTOR_PRIMARY)
    {
        uint16_t block_size = read_le16(buffer + 128);
        uint16_t block_size_be = read_be16(buffer + 130);

        if (block_size == 0)
            return false;

        if (block_size != block_size_be)
            return false;

        if (block_size < PHYSICAL_SECTOR_SIZE)
            return false;

        if (block_size > MAX_LOGICAL_BLOCK_SIZE)
            return false;

        if ((block_size % PHYSICAL_SECTOR_SIZE) != 0)
            return false;

        uint32_t space = read_le32(buffer + 80);
        uint32_t space_be = read_be32(buffer + 84);

        if (space == 0 || space != space_be)
            return false;

        uint8_t root_length = buffer[156];
        if (root_length < 34)
            return false;

        if (156U + root_length > logical_block_size_)
            return false;

        uint32_t root_lba = read_le32(buffer + 158);
        uint32_t root_lba_be = read_be32(buffer + 162);
        uint32_t root_size = read_le32(buffer + 166);
        uint32_t root_size_be = read_be32(buffer + 170);

        if (root_lba != root_lba_be)
            return false;

        if (root_size != root_size_be)
            return false;

        if (root_lba >= space)
            return false;

        if (ceil_div_u64(root_size, block_size) >
            static_cast<uint64_t>(space) - root_lba)
        {
            return false;
        }

        logical_block_size_ = block_size;
        volume_space_size_ = space;
        root_extent_lba_ = root_lba;
        root_data_length_ = root_size;
        root_flags_ = buffer[181];
        descriptor_type_ = DESCRIPTOR_PRIMARY;

        for (size_t i = 0; i < 32; ++i)
        {
            char c = static_cast<char>(buffer[40 + i]);

            if (c == ' ')
                c = '\0';

            volume_identifier_[i] = c;

            if (c == '\0')
            {
                for (size_t j = i + 1; j < 33; ++j)
                    volume_identifier_[j] = '\0';
                break;
            }
        }

        volume_identifier_[32] = '\0';
        valid = true;
        return true;
    }

    if (type == DESCRIPTOR_SUPPLEMENTARY)
    {
        joliet_candidate = joliet_level(buffer + 88) != 0;
        valid = joliet_candidate;
        return true;
    }

    return false;
}

bool ISO9660Reader::parse_root_record(
    const uint8_t* record,
    size_t available,
    uint32_t& extent_lba,
    uint32_t& data_length,
    uint8_t& flags) const
{
    if (!record || available < 34)
        return false;

    uint8_t record_length = record[0];

    if (record_length < 34 || record_length > available)
        return false;

    if (record[32] != 1 || record[33] != 0)
        return false;

    uint32_t lba = read_le32(record + 2);
    uint32_t lba_be = read_be32(record + 6);
    uint32_t size = read_le32(record + 10);
    uint32_t size_be = read_be32(record + 14);

    if (lba != lba_be || size != size_be)
        return false;

    extent_lba = lba;
    data_length = size;
    flags = record[25];

    return true;
}

bool ISO9660Reader::decode_iso_name(
    const uint8_t* source,
    size_t source_length,
    char* destination,
    size_t destination_capacity) const
{
    if (!source || !destination || destination_capacity == 0)
        return false;

    size_t copied = 0;

    for (size_t i = 0; i < source_length; ++i)
    {
        if (copied + 1 >= destination_capacity)
            break;

        char c = static_cast<char>(source[i]);

        /* ISO 9660 special current/parent entries are not exposed as names. */
        if (source_length == 1 && (source[0] == 0 || source[0] == 1))
            return false;

        destination[copied++] = c;
    }

    destination[copied] = '\0';

    /* Remove ISO version suffix: FILE.TXT;1 or FILE.TXT;32768. */
    for (size_t i = 0; i < copied; ++i)
    {
        if (destination[i] != ';')
            continue;

        bool valid_version = (i + 1 < copied);
        size_t j = i + 1;

        while (j < copied)
        {
            if (!is_ascii_digit(destination[j]))
            {
                valid_version = false;
                break;
            }

            ++j;
        }

        if (valid_version)
        {
            destination[i] = '\0';
            copied = i;
        }

        break;
    }

    /* ISO implementations sometimes emit a trailing dot. */
    while (copied > 0 && destination[copied - 1] == '.')
    {
        destination[copied - 1] = '\0';
        --copied;
    }

    return copied != 0;
}

bool ISO9660Reader::decode_joliet_name(
    const uint8_t* source,
    size_t source_length,
    char* destination,
    size_t destination_capacity) const
{
    if (!source || !destination || destination_capacity == 0)
        return false;

    if ((source_length & 1U) != 0)
        return false;

    size_t output = 0;

    for (size_t i = 0; i < source_length; i += 2)
    {
        uint16_t code = read_be16(source + i);

        if (code == 0)
            break;

        if (code == 0 || (source_length == 2 && (code == 0 || code == 1)))
            return false;

        /* Strip the ISO version suffix for Joliet as well. */
        if (code == ';')
        {
            bool version = (i + 2 < source_length);
            size_t j = i + 2;

            while (j < source_length)
            {
                uint16_t v = read_be16(source + j);
                if (v < '0' || v > '9')
                {
                    version = false;
                    break;
                }
                j += 2;
            }

            if (version)
                break;
        }

        uint32_t cp = code;

        if (code >= 0xD800 && code <= 0xDBFF && i + 3 < source_length)
        {
            uint16_t low = read_be16(source + i + 2);
            if (low >= 0xDC00 && low <= 0xDFFF)
            {
                cp = 0x10000U +
                     ((static_cast<uint32_t>(code) - 0xD800U) << 10) +
                     (static_cast<uint32_t>(low) - 0xDC00U);
                i += 2;
            }
        }

        if (cp <= 0x7FU)
        {
            if (output + 1 >= destination_capacity)
                return false;

            destination[output++] = static_cast<char>(cp);
        }
        else if (cp <= 0x7FFU)
        {
            if (output + 2 >= destination_capacity)
                return false;

            destination[output++] =
                static_cast<char>(0xC0U | (cp >> 6));
            destination[output++] =
                static_cast<char>(0x80U | (cp & 0x3FU));
        }
        else if (cp <= 0xFFFFU)
        {
            if (output + 3 >= destination_capacity)
                return false;

            destination[output++] =
                static_cast<char>(0xE0U | (cp >> 12));
            destination[output++] =
                static_cast<char>(0x80U | ((cp >> 6) & 0x3FU));
            destination[output++] =
                static_cast<char>(0x80U | (cp & 0x3FU));
        }
        else if (cp <= 0x10FFFFU)
        {
            if (output + 4 >= destination_capacity)
                return false;

            destination[output++] =
                static_cast<char>(0xF0U | (cp >> 18));
            destination[output++] =
                static_cast<char>(0x80U | ((cp >> 12) & 0x3FU));
            destination[output++] =
                static_cast<char>(0x80U | ((cp >> 6) & 0x3FU));
            destination[output++] =
                static_cast<char>(0x80U | (cp & 0x3FU));
        }
        else
        {
            return false;
        }
    }

    if (output == 0)
        return false;

    destination[output] = '\0';
    return true;
}

bool ISO9660Reader::parse_rock_ridge_name(
    const uint8_t* system_use,
    size_t system_use_length,
    char* destination,
    size_t destination_capacity) const
{
    if (!system_use || !destination || destination_capacity == 0)
        return false;

    size_t position = 0;
    size_t output = 0;
    bool found = false;

    while (position + 4 <= system_use_length)
    {
        const uint8_t* entry = system_use + position;
        uint8_t length = entry[2];

        if (length < 4)
            break;

        if (position + length > system_use_length)
            break;

        /* ST = end of system use area. */
        if (entry[0] == 'S' && entry[1] == 'T')
            break;

        /* NM = Rock Ridge alternate name. */
        if (entry[0] == 'N' && entry[1] == 'M' && length >= 5)
        {
            uint8_t flags = entry[4];

            /* CURRENT/PARENT/ROOT are not ordinary filenames. */
            if ((flags & 0x0E) == 0)
            {
                size_t name_length = static_cast<size_t>(length) - 5;

                for (size_t i = 0; i < name_length; ++i)
                {
                    if (output + 1 >= destination_capacity)
                        return false;

                    destination[output++] =
                        static_cast<char>(entry[5 + i]);
                }

                found = true;
            }
        }

        position += length;
    }

    destination[output] = '\0';
    return found && output != 0;
}

bool ISO9660Reader::parse_directory_record(
    const uint8_t* record,
    size_t available,
    ISO9660DirectoryEntry& out) const
{
    clear_entry(out);

    if (!record || available < 34)
        return false;

    uint8_t record_length = record[0];

    if (record_length < 34 || record_length > available)
        return false;

    uint8_t name_length = record[32];

    if (33U + static_cast<size_t>(name_length) > record_length)
        return false;

    uint32_t extent_lba = read_le32(record + 2);
    uint32_t extent_lba_be = read_be32(record + 6);
    uint32_t data_length = read_le32(record + 10);
    uint32_t data_length_be = read_be32(record + 14);

    if (extent_lba != extent_lba_be)
        return false;

    if (data_length != data_length_be)
        return false;

    out.extent_lba = extent_lba;
    out.size = data_length;
    out.flags = record[25];
    out.file_unit_size = record[26];
    out.interleave_gap_size = record[27];
    out.volume_sequence = read_le16(record + 28);

    if (out.volume_sequence != read_be16(record + 30))
        return false;

    out.directory = (out.flags & FILE_FLAG_DIRECTORY) != 0;
    out.hidden = (out.flags & FILE_FLAG_HIDDEN) != 0;
    out.associated = (out.flags & FILE_FLAG_ASSOCIATED) != 0;
    out.multi_extent = (out.flags & FILE_FLAG_MULTI_EXTENT) != 0;

    if (out.extent_lba >= volume_space_size_ && out.size != 0)
        return false;

    if (ceil_div_u64(out.size, logical_block_size_) >
        static_cast<uint64_t>(volume_space_size_) - out.extent_lba)
    {
        return false;
    }

    /* ISO/Joliet filename data. */
    const uint8_t* identifier = record + 33;
    size_t identifier_length = name_length;

    if (identifier_length == 1 &&
        (identifier[0] == 0 || identifier[0] == 1))
    {
        return false;
    }

    bool decoded = false;

    if (joliet_)
    {
        decoded = decode_joliet_name(
            identifier,
            identifier_length,
            out.name,
            sizeof(out.name));
    }
    else
    {
        decoded = decode_iso_name(
            identifier,
            identifier_length,
            out.name,
            sizeof(out.name));
    }

    if (!decoded)
        return false;

    /*
     * System Use Area starts after identifier and its padding byte.
     * ISO records are even-length aligned.
     */
    size_t system_use_offset =
        33U + static_cast<size_t>(name_length);

    if ((name_length & 1U) == 0)
        ++system_use_offset;

    if (system_use_offset < record_length)
    {
        const uint8_t* system_use = record + system_use_offset;
        size_t system_use_length =
            static_cast<size_t>(record_length) - system_use_offset;

        char rock_name[256];

        if (parse_rock_ridge_name(
                system_use,
                system_use_length,
                rock_name,
                sizeof(rock_name)))
        {
            size_t i = 0;
            for (; i + 1 < sizeof(out.name) && rock_name[i] != '\0'; ++i)
                out.name[i] = rock_name[i];

            out.name[i] = '\0';
            out.rock_ridge = true;
        }
    }

    return true;
}

bool ISO9660Reader::scan_rock_ridge_root()
{
    if (!mounted_)
        return false;

    if (logical_block_size_ == 0 || logical_block_size_ > MAX_LOGICAL_BLOCK_SIZE)
        return false;

    alignas(4096) uint8_t block[MAX_LOGICAL_BLOCK_SIZE];

    uint64_t remaining = root_data_length_;
    uint64_t lba = root_extent_lba_;

    while (remaining != 0)
    {
        uint64_t chunk = remaining;
        if (chunk > logical_block_size_)
            chunk = logical_block_size_;

        if (!read_logical_block(lba, block))
            return false;

        size_t offset = 0;
        size_t limit = static_cast<size_t>(chunk);

        while (offset < limit)
        {
            uint8_t record_length = block[offset];

            if (record_length == 0)
                break;

            if (record_length < 34 || offset + record_length > limit)
                break;

            uint8_t name_length = block[offset + 32];
            size_t su_offset = 33U + static_cast<size_t>(name_length);
            if ((name_length & 1U) == 0)
                ++su_offset;

            if (su_offset < record_length)
            {
                const uint8_t* su = block + offset + su_offset;
                size_t su_len =
                    static_cast<size_t>(record_length) - su_offset;

                size_t su_pos = 0;
                while (su_pos + 4 <= su_len)
                {
                    const uint8_t* e = su + su_pos;
                    uint8_t e_len = e[2];

                    if (e_len < 4 || su_pos + e_len > su_len)
                        break;

                    if (e[0] == 'N' && e[1] == 'M')
                    {
                        rock_ridge_ = true;
                        return true;
                    }

                    /* SP: Rock Ridge extension marker. */
                    if (e[0] == 'S' && e[1] == 'P' && e_len >= 7 &&
                        e[4] == 0xBE && e[5] == 0xEF)
                    {
                        rock_ridge_ = true;
                        return true;
                    }

                    if (e[0] == 'S' && e[1] == 'T')
                        break;

                    su_pos += e_len;
                }
            }

            offset += record_length;
        }

        if (remaining <= chunk)
            break;

        remaining -= chunk;
        ++lba;
    }

    return true;
}

bool ISO9660Reader::initialize()
{
    mounted_ = false;
    joliet_ = false;
    rock_ridge_ = false;
    logical_block_size_ = 0;
    volume_space_size_ = 0;
    root_extent_lba_ = 0;
    root_data_length_ = 0;
    root_flags_ = 0;
    descriptor_type_ = 0;

    for (size_t i = 0; i < sizeof(volume_identifier_); ++i)
        volume_identifier_[i] = '\0';

    if (!vfs_blk_adapter::init_backend())
        return false;

    /*
     * The descriptor block size cannot be known until block 16 is read.
     * ISO 9660 uses 2048-byte sectors in the volume descriptor area in
     * normal images. BlockOS's backend exposes 512-byte sectors, so read
     * the four physical sectors making up descriptor 16 first.
     */
    alignas(4096) uint8_t descriptor_probe[2048];

    if (!vfs_blk_adapter::read_sectors_to_vfs(
            static_cast<uint64_t>(ISO_DESCRIPTOR_START) * 4ULL,
            4,
            descriptor_probe))
    {
        return false;
    }

    if (!signature_is_cd001(descriptor_probe))
        return false;

    uint8_t first_type = descriptor_probe[0];

    if (first_type != DESCRIPTOR_PRIMARY)
        return false;

    uint16_t pvd_block_size = read_le16(descriptor_probe + 128);
    uint16_t pvd_block_size_be = read_be16(descriptor_probe + 130);

    if (pvd_block_size == 0 || pvd_block_size != pvd_block_size_be)
        return false;

    if (pvd_block_size < PHYSICAL_SECTOR_SIZE ||
        pvd_block_size > MAX_LOGICAL_BLOCK_SIZE ||
        (pvd_block_size % PHYSICAL_SECTOR_SIZE) != 0)
    {
        return false;
    }

    logical_block_size_ = pvd_block_size;

    alignas(4096) uint8_t block[MAX_LOGICAL_BLOCK_SIZE];

    bool pvd_valid = false;
    bool ignored_joliet_candidate = false;

    if (!read_volume_descriptor(ISO_DESCRIPTOR_START, block))
        return false;

    if (!parse_descriptor(
            block,
            DESCRIPTOR_PRIMARY,
            pvd_valid,
            ignored_joliet_candidate) ||
        !pvd_valid)
    {
        return false;
    }

    /*
     * Search all supplementary descriptors and prefer the highest Joliet
     * level. We keep a copy of the currently best descriptor because its
     * root directory may differ from the PVD root.
     */
    uint8_t best_joliet_level = 0;
    uint64_t descriptor_lba = ISO_DESCRIPTOR_START + 1;
    bool terminated = false;

    while (descriptor_lba < ISO_DESCRIPTOR_START + 256)
    {
        if (!read_volume_descriptor(descriptor_lba, block))
            return false;

        uint8_t type = block[0];

        if (!signature_is_cd001(block))
            return false;

        if (block[6] != 1)
            return false;

        if (type == DESCRIPTOR_TERMINATOR)
        {
            terminated = true;
            break;
        }

        if (type == DESCRIPTOR_SUPPLEMENTARY)
        {
            int level = joliet_level(block + 88);

            if (level > static_cast<int>(best_joliet_level))
            {
                best_joliet_level = static_cast<uint8_t>(level);

                uint16_t svd_block_size = read_le16(block + 128);
                uint16_t svd_block_size_be = read_be16(block + 130);
                uint32_t svd_space = read_le32(block + 80);
                uint32_t svd_space_be = read_be32(block + 84);

                if (svd_block_size == logical_block_size_ &&
                    svd_block_size == svd_block_size_be &&
                    svd_space == svd_space_be &&
                    svd_space != 0)
                {
                    uint8_t root_record_length = block[156];

                    if (root_record_length >= 34 &&
                        156U + root_record_length <= logical_block_size_)
                    {
                        uint32_t root_lba = 0;
                        uint32_t root_size = 0;
                        uint8_t root_flags = 0;

                        if (parse_root_record(
                                block + 156,
                                root_record_length,
                                root_lba,
                                root_size,
                                root_flags) &&
                            root_lba < svd_space &&
                            ceil_div_u64(root_size, logical_block_size_) <=
                                static_cast<uint64_t>(svd_space) - root_lba)
                        {
                            joliet_ = true;
                            logical_block_size_ = svd_block_size;
                            volume_space_size_ = svd_space;
                            root_extent_lba_ = root_lba;
                            root_data_length_ = root_size;
                            root_flags_ = root_flags;
                            descriptor_type_ = DESCRIPTOR_SUPPLEMENTARY;

                            for (size_t i = 0; i < 32; ++i)
                            {
                                char c = static_cast<char>(block[40 + i]);
                                if (c == ' ')
                                    c = '\0';

                                volume_identifier_[i] = c;

                                if (c == '\0')
                                {
                                    for (size_t j = i + 1; j < 33; ++j)
                                        volume_identifier_[j] = '\0';
                                    break;
                                }
                            }

                            volume_identifier_[32] = '\0';
                        }
                    }
                }
            }
        }

        ++descriptor_lba;
    }

    if (!terminated)
        return false;

    /* ISO 9660 level 1/2/3 images must still have a usable root. */
    if (logical_block_size_ < PHYSICAL_SECTOR_SIZE ||
        logical_block_size_ > MAX_LOGICAL_BLOCK_SIZE ||
        volume_space_size_ == 0 ||
        root_extent_lba_ >= volume_space_size_)
    {
        return false;
    }

    mounted_ = true;

    /* Rock Ridge is optional; failure to detect it is not a mount failure. */
    scan_rock_ridge_root();

    return true;
}

bool ISO9660Reader::mount()
{
    return initialize();
}

bool ISO9660Reader::unmount()
{
    mounted_ = false;
    joliet_ = false;
    rock_ridge_ = false;
    logical_block_size_ = 0;
    volume_space_size_ = 0;
    root_extent_lba_ = 0;
    root_data_length_ = 0;
    root_flags_ = 0;
    descriptor_type_ = 0;

    for (size_t i = 0; i < sizeof(volume_identifier_); ++i)
        volume_identifier_[i] = '\0';

    return true;
}

bool ISO9660Reader::ready() const
{
    return mounted_;
}

bool ISO9660Reader::is_iso9660() const
{
    return mounted_;
}

bool ISO9660Reader::is_joliet() const
{
    return mounted_ && joliet_;
}

bool ISO9660Reader::has_rock_ridge() const
{
    return mounted_ && rock_ridge_;
}

uint32_t ISO9660Reader::logical_block_size() const
{
    return logical_block_size_;
}

uint64_t ISO9660Reader::volume_space_size() const
{
    return volume_space_size_;
}

const char* ISO9660Reader::volume_identifier() const
{
    return volume_identifier_;
}

uint64_t ISO9660Reader::root_extent_lba() const
{
    return root_extent_lba_;
}

uint64_t ISO9660Reader::root_directory_size() const
{
    return root_data_length_;
}

bool ISO9660Reader::string_equals_component(
    const char* requested,
    const char* stored) const
{
    if (!requested || !stored)
        return false;

    /* ASCII path names are case-insensitive in normal ISO 9660 usage. */
    return strings_equal_case_insensitive_ascii(requested, stored);
}

bool ISO9660Reader::find_in_directory(
    uint64_t directory_lba,
    uint64_t directory_size,
    const char* component,
    ISO9660DirectoryEntry& out) const
{
    if (!mounted_ || !component || component[0] == '\0')
        return false;

    if (directory_lba >= volume_space_size_ && directory_size != 0)
        return false;

    if (ceil_div_u64(directory_size, logical_block_size_) >
        static_cast<uint64_t>(volume_space_size_) -
            (directory_lba < volume_space_size_ ? directory_lba : volume_space_size_))
    {
        return false;
    }

    alignas(4096) uint8_t block[MAX_LOGICAL_BLOCK_SIZE];

    uint64_t remaining = directory_size;
    uint64_t lba = directory_lba;

    while (remaining != 0)
    {
        if (!read_logical_block(lba, block))
            return false;

        uint64_t block_bytes = remaining;
        if (block_bytes > logical_block_size_)
            block_bytes = logical_block_size_;

        size_t offset = 0;
        size_t limit = static_cast<size_t>(block_bytes);

        while (offset < limit)
        {
            uint8_t record_length = block[offset];

            if (record_length == 0)
                break;

            if (record_length < 34 ||
                offset + static_cast<size_t>(record_length) > limit)
            {
                return false;
            }

            ISO9660DirectoryEntry entry;

            if (parse_directory_record(
                    block + offset,
                    limit - offset,
                    entry))
            {
                if (string_equals_component(component, entry.name))
                {
                    out = entry;
                    return true;
                }
            }

            offset += record_length;
        }

        if (remaining <= block_bytes)
            break;

        remaining -= block_bytes;
        ++lba;
    }

    return false;
}

bool ISO9660Reader::resolve_path(
    const char* path,
    ISO9660DirectoryEntry& out) const
{
    if (!mounted_ || !path)
        return false;

    size_t path_length = string_length(path);

    if (path_length == 0)
        return false;

    if (path[0] != '/')
        return false;

    if (path[1] == '\0')
    {
        clear_entry(out);
        out.name[0] = '/';
        out.name[1] = '\0';
        out.extent_lba = root_extent_lba_;
        out.size = root_data_length_;
        out.flags = root_flags_ | FILE_FLAG_DIRECTORY;
        out.directory = true;
        return true;
    }

    uint64_t current_lba = root_extent_lba_;
    uint64_t current_size = root_data_length_;

    size_t position = 1;

    while (position < path_length)
    {
        while (position < path_length && path[position] == '/')
            ++position;

        if (position >= path_length)
            break;

        char component[256];
        size_t component_length = 0;

        while (position < path_length && path[position] != '/')
        {
            if (component_length + 1 >= sizeof(component))
                return false;

            component[component_length++] = path[position++];
        }

        component[component_length] = '\0';

        if (component_length == 0)
            continue;

        if (strings_equal_case_insensitive_ascii(component, "."))
            continue;

        if (strings_equal_case_insensitive_ascii(component, ".."))
            return false;

        ISO9660DirectoryEntry found;

        if (!find_in_directory(
                current_lba,
                current_size,
                component,
                found))
        {
            return false;
        }

        bool last = true;
        size_t lookahead = position;
        while (lookahead < path_length && path[lookahead] == '/')
            ++lookahead;
        if (lookahead < path_length)
            last = false;

        if (!last && !found.directory)
            return false;

        if (last)
        {
            out = found;
            return true;
        }

        current_lba = found.extent_lba;
        current_size = found.size;
    }

    return false;
}

bool ISO9660Reader::resolve_parent_directory(
    const char* path,
    uint64_t& parent_lba,
    uint64_t& parent_size,
    char* leaf,
    size_t leaf_capacity) const
{
    if (!mounted_ || !path || !leaf || leaf_capacity == 0)
        return false;

    size_t path_length = string_length(path);

    if (path_length < 2 || path[0] != '/')
        return false;

    size_t end = path_length;
    while (end > 1 && path[end - 1] == '/')
        --end;

    if (end <= 1)
        return false;

    size_t slash = end;

    while (slash > 1 && path[slash - 1] != '/')
        --slash;

    size_t leaf_length = end - slash;

    if (leaf_length == 0 || leaf_length + 1 > leaf_capacity)
        return false;

    for (size_t i = 0; i < leaf_length; ++i)
        leaf[i] = path[slash + i];
    leaf[leaf_length] = '\0';

    if (slash == 1)
    {
        parent_lba = root_extent_lba_;
        parent_size = root_data_length_;
        return true;
    }

    char parent_path[512];

    if (slash >= sizeof(parent_path))
        return false;

    parent_path[0] = '/';

    size_t parent_len = slash - 1;

    for (size_t i = 0; i < parent_len; ++i)
        parent_path[i + 1] = path[i + 1];

    parent_path[parent_len + 1] = '\0';

    ISO9660DirectoryEntry parent;

    if (!resolve_path(parent_path, parent))
        return false;

    if (!parent.directory)
        return false;

    parent_lba = parent.extent_lba;
    parent_size = parent.size;

    return true;
}

bool ISO9660Reader::read_extent(
    uint64_t extent_lba,
    uint64_t file_size,
    uint8_t* destination,
    uint64_t destination_capacity,
    uint64_t& bytes_read) const
{
    bytes_read = 0;

    if (!destination && file_size != 0)
        return false;

    if (file_size > destination_capacity)
        return false;

    if (extent_lba >= volume_space_size_ && file_size != 0)
        return false;

    if (ceil_div_u64(file_size, logical_block_size_) >
        static_cast<uint64_t>(volume_space_size_) - extent_lba)
    {
        return false;
    }

    if (file_size == 0)
        return true;

    alignas(4096) uint8_t block[MAX_LOGICAL_BLOCK_SIZE];

    uint64_t remaining = file_size;
    uint64_t lba = extent_lba;

    while (remaining != 0)
    {
        if (!read_logical_block(lba, block))
            return false;

        uint64_t chunk = remaining;
        if (chunk > logical_block_size_)
            chunk = logical_block_size_;

        for (uint64_t i = 0; i < chunk; ++i)
            destination[bytes_read + i] = block[i];

        bytes_read += chunk;
        remaining -= chunk;
        ++lba;
    }

    return true;
}

bool ISO9660Reader::get_entry(
    const char* path,
    ISO9660DirectoryEntry& out) const
{
    return resolve_path(path, out);
}

bool ISO9660Reader::read_file(
    const char* path,
    uint8_t* destination,
    size_t max_size,
    size_t* bytes_read) const
{
    if (bytes_read)
        *bytes_read = 0;

    ISO9660DirectoryEntry entry;

    if (!resolve_path(path, entry))
        return false;

    if (entry.directory)
        return false;

    /* Interleaved files need special handling that BlockOS does not need. */
    if (entry.file_unit_size != 0 || entry.interleave_gap_size != 0)
        return false;

    if (entry.multi_extent)
        return false;

    uint64_t read = 0;

    if (!read_extent(
            entry.extent_lba,
            entry.size,
            destination,
            static_cast<uint64_t>(max_size),
            read))
    {
        return false;
    }

    if (bytes_read)
        *bytes_read = static_cast<size_t>(read);

    return true;
}

bool ISO9660Reader::list_directory(
    const char* path,
    ISO9660DirectoryEntry* entries,
    size_t capacity,
    size_t* count) const
{
    if (count)
        *count = 0;

    ISO9660DirectoryEntry directory;

    if (!resolve_path(path, directory))
        return false;

    if (!directory.directory)
        return false;

    if (!entries && capacity != 0)
        return false;

    alignas(4096) uint8_t block[MAX_LOGICAL_BLOCK_SIZE];

    uint64_t remaining = directory.size;
    uint64_t lba = directory.extent_lba;
    size_t output_count = 0;

    while (remaining != 0)
    {
        if (!read_logical_block(lba, block))
            return false;

        uint64_t block_bytes = remaining;
        if (block_bytes > logical_block_size_)
            block_bytes = logical_block_size_;

        size_t offset = 0;
        size_t limit = static_cast<size_t>(block_bytes);

        while (offset < limit)
        {
            uint8_t record_length = block[offset];

            if (record_length == 0)
                break;

            if (record_length < 34 ||
                offset + static_cast<size_t>(record_length) > limit)
            {
                return false;
            }

            ISO9660DirectoryEntry entry;

            if (parse_directory_record(
                    block + offset,
                    limit - offset,
                    entry))
            {
                /* Skip special current/parent records automatically. */
                if (entry.name[0] != '\0')
                {
                    if (output_count < capacity)
                    {
                        entries[output_count] = entry;
                        ++output_count;
                    }
                }
            }

            offset += record_length;
        }

        if (remaining <= block_bytes)
            break;

        remaining -= block_bytes;
        ++lba;
    }

    if (count)
        *count = output_count;

    return true;
}

ISO9660Reader iso9660_fs;

bool iso9660_init()
{
    return iso9660_fs.initialize();
}

bool iso9660_read_file(
    const char* path,
    uint8_t* destination,
    size_t max_size,
    size_t* bytes_read)
{
    return iso9660_fs.read_file(
        path,
        destination,
        max_size,
        bytes_read);
}

} // namespace iso9660
