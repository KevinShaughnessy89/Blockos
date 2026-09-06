
#include <stdint.h>
#include <stddef.h>

namespace blockos::smbios
{

// ------------------------------------------------------------
// Public API
// ------------------------------------------------------------

struct Info
{
    const char* bios_vendor;
    const char* bios_version;
    const char* bios_date;

    const char* manufacturer;
    const char* product;
    const char* version;
    const char* serial;
    const char* sku;
    const char* family;

    const char* board_manufacturer;
    const char* board_product;
    const char* board_version;
    const char* board_serial;
    const char* board_asset_tag;

    uint8_t major_version;
    uint8_t minor_version;

    bool valid;
};

static Info g_info{};

// ------------------------------------------------------------
// SMBIOS structures
// ------------------------------------------------------------

struct SMBIOSHeader
{
    uint8_t type;
    uint8_t length;
    uint16_t handle;
};

struct SMBIOS2Entry
{
    char signature[4];          // "_SM_"
    uint8_t checksum;
    uint8_t length;
    uint8_t major;
    uint8_t minor;
    uint16_t max_structure_size;
    uint8_t entry_revision;
    uint8_t formatted_area[5];

    char intermediate_anchor[5]; // "_DMI_"
    uint8_t intermediate_checksum;
    uint16_t table_length;
    uint32_t table_address;
    uint16_t structure_count;
    uint8_t bcd_revision;
};

struct SMBIOS3Entry
{
    char signature[5];          // "_SM3_"
    uint8_t checksum;
    uint8_t length;
    uint8_t major;
    uint8_t minor;
    uint8_t docrev;
    uint8_t entry_revision;
    uint8_t reserved;
    uint64_t table_address;
    uint32_t table_max_size;
};

// ------------------------------------------------------------
// Safe memory access
// ------------------------------------------------------------

static volatile uint8_t read8(uintptr_t address)
{
    return *reinterpret_cast<volatile uint8_t*>(address);
}

static volatile uint16_t read16(uintptr_t address)
{
    return *reinterpret_cast<volatile uint16_t*>(address);
}

static volatile uint32_t read32(uintptr_t address)
{
    return *reinterpret_cast<volatile uint32_t*>(address);
}

static volatile uint64_t read64(uintptr_t address)
{
    return *reinterpret_cast<volatile uint64_t*>(address);
}

static bool memory_equal(
    uintptr_t address,
    const char* value,
    size_t length)
{
    for (size_t i = 0; i < length; ++i)
    {
        if (read8(address + i) !=
            static_cast<uint8_t>(value[i]))
        {
            return false;
        }
    }

    return true;
}

// ------------------------------------------------------------
// Checksum
// ------------------------------------------------------------

static bool checksum(
    uintptr_t address,
    size_t length)
{
    uint8_t sum = 0;

    for (size_t i = 0; i < length; ++i)
        sum = static_cast<uint8_t>(
            sum + read8(address + i));

    return sum == 0;
}

// ------------------------------------------------------------
// String helpers
// ------------------------------------------------------------

static const char* empty_string()
{
    return "";
}

static const char* get_string(
    uintptr_t structure,
    uint8_t structure_length,
    uint8_t index)
{
    if (index == 0)
        return empty_string();

    uintptr_t string_table =
        structure + structure_length;

    uint8_t current = 1;

    while (true)
    {
        uint8_t first = read8(string_table);

        if (first == 0)
        {
            if (read8(string_table + 1) == 0)
                return empty_string();

            ++string_table;
            ++current;
            continue;
        }

        if (current == index)
        {
            return reinterpret_cast<const char*>(string_table);
        }

        while (read8(string_table) != 0)
            ++string_table;

        ++string_table;
        ++current;
    }
}

// ------------------------------------------------------------
// Internal string storage
//
// SMBIOS strings live in firmware memory, so the returned
// pointers refer directly to the SMBIOS table.
// ------------------------------------------------------------

static void clear_info()
{
    g_info.bios_vendor = "";
    g_info.bios_version = "";
    g_info.bios_date = "";

    g_info.manufacturer = "";
    g_info.product = "";
    g_info.version = "";
    g_info.serial = "";
    g_info.sku = "";
    g_info.family = "";

    g_info.board_manufacturer = "";
    g_info.board_product = "";
    g_info.board_version = "";
    g_info.board_serial = "";
    g_info.board_asset_tag = "";

    g_info.major_version = 0;
    g_info.minor_version = 0;
    g_info.valid = false;
}

// ------------------------------------------------------------
// Parse Type 0 - BIOS Information
// ------------------------------------------------------------

static void parse_type0(
    uintptr_t address,
    uint8_t length)
{
    if (length < 0x12)
        return;

    uint8_t vendor =
        read8(address + 0x04);

    uint8_t version =
        read8(address + 0x05);

    uint8_t date =
        read8(address + 0x08);

    g_info.bios_vendor =
        get_string(address, length, vendor);

    g_info.bios_version =
        get_string(address, length, version);

    g_info.bios_date =
        get_string(address, length, date);
}

// ------------------------------------------------------------
// Parse Type 1 - System Information
// ------------------------------------------------------------

static void parse_type1(
    uintptr_t address,
    uint8_t length)
{
    if (length < 0x08)
        return;

    uint8_t manufacturer =
        read8(address + 0x04);

    uint8_t product =
        read8(address + 0x05);

    uint8_t version =
        read8(address + 0x06);

    uint8_t serial =
        read8(address + 0x07);

    g_info.manufacturer =
        get_string(address, length, manufacturer);

    g_info.product =
        get_string(address, length, product);

    g_info.version =
        get_string(address, length, version);

    g_info.serial =
        get_string(address, length, serial);

    if (length >= 0x19)
    {
        uint8_t sku =
            read8(address + 0x19);

        g_info.sku =
            get_string(address, length, sku);
    }

    if (length >= 0x1A)
    {
        uint8_t family =
            read8(address + 0x1A);

        g_info.family =
            get_string(address, length, family);
    }
}

// ------------------------------------------------------------
// Parse Type 2 - Baseboard Information
// ------------------------------------------------------------

static void parse_type2(
    uintptr_t address,
    uint8_t length)
{
    if (length < 0x08)
        return;

    uint8_t manufacturer =
        read8(address + 0x04);

    uint8_t product =
        read8(address + 0x05);

    uint8_t version =
        read8(address + 0x06);

    uint8_t serial =
        read8(address + 0x07);

    g_info.board_manufacturer =
        get_string(address, length, manufacturer);

    g_info.board_product =
        get_string(address, length, product);

    g_info.board_version =
        get_string(address, length, version);

    g_info.board_serial =
        get_string(address, length, serial);

    if (length >= 0x09)
    {
        uint8_t asset =
            read8(address + 0x08);

        g_info.board_asset_tag =
            get_string(address, length, asset);
    }
}

// ------------------------------------------------------------
// Parse one SMBIOS structure
// ------------------------------------------------------------

static bool parse_structure(
    uintptr_t address)
{
    SMBIOSHeader header{};

    header.type =
        read8(address + 0);

    header.length =
        read8(address + 1);

    header.handle =
        read16(address + 2);

    if (header.length < 4)
        return false;

    switch (header.type)
    {
        case 0:
            parse_type0(
                address,
                header.length);
            break;

        case 1:
            parse_type1(
                address,
                header.length);
            break;

        case 2:
            parse_type2(
                address,
                header.length);
            break;

        case 127:
            return false;

        default:
            break;
    }

    return true;
}

// ------------------------------------------------------------
// Find end of SMBIOS structure
// ------------------------------------------------------------

static uintptr_t structure_end(
    uintptr_t address)
{
    uint8_t length =
        read8(address + 1);

    uintptr_t p =
        address + length;

    while (true)
    {
        uint8_t a = read8(p);
        uint8_t b = read8(p + 1);

        if (a == 0 && b == 0)
            return p + 2;

        ++p;
    }
}

// ------------------------------------------------------------
// Parse SMBIOS structure table
// ------------------------------------------------------------

static bool parse_table(
    uintptr_t table,
    uint32_t table_length,
    uint16_t structure_count)
{
    if (table == 0)
        return false;

    uintptr_t current = table;
    uintptr_t end =
        table + table_length;

    uint16_t parsed = 0;

    while (current + 4 <= end &&
           parsed < structure_count)
    {
        uint8_t type =
            read8(current + 0);

        uint8_t length =
            read8(current + 1);

        if (length < 4)
            return false;

        if (!parse_structure(current))
            break;

        uintptr_t next =
            structure_end(current);

        if (next <= current || next > end)
            return false;

        current = next;
        ++parsed;

        if (type == 127)
            break;
    }

    return parsed != 0;
}

// ------------------------------------------------------------
// Scan BIOS area for SMBIOS 2.x
// ------------------------------------------------------------

static bool find_smbios2(
    uintptr_t& table,
    uint32_t& table_length,
    uint16_t& count,
    uint8_t& major,
    uint8_t& minor)
{
    // SMBIOS 2.x entry point is normally in
    // 0x000F0000 - 0x000FFFFF.
    //
    // Search every 16 bytes.

    for (uintptr_t address = 0x000F0000;
         address < 0x00100000;
         address += 16)
    {
        if (!memory_equal(
                address,
                "_SM_",
                4))
        {
            continue;
        }

        uint8_t length =
            read8(address + 0x05);

        if (length < 0x1F)
            continue;

        if (!checksum(address, length))
            continue;

        if (!memory_equal(
                address + 0x10,
                "_DMI_",
                5))
        {
            continue;
        }

        uint8_t dmi_length =
            read8(address + 0x15);

        if (!checksum(
                address + 0x10,
                dmi_length))
        {
            continue;
        }

        major =
            read8(address + 0x06);

        minor =
            read8(address + 0x07);

        table_length =
            read16(address + 0x16);

        table =
            read32(address + 0x18);

        count =
            read16(address + 0x1C);

        if (table == 0 ||
            table_length == 0 ||
            count == 0)
        {
            continue;
        }

        return true;
    }

    return false;
}

// ------------------------------------------------------------
// Scan high memory for SMBIOS 3.x
//
// SMBIOS 3 entry point can be located by EFI configuration
// tables, but without an EFI table pointer being passed into
// this file, a conservative physical scan is used here.
// ------------------------------------------------------------

static bool find_smbios3(
    uintptr_t& table,
    uint32_t& table_length,
    uint8_t& major,
    uint8_t& minor)
{
    // Common firmware implementations place the SMBIOS 3
    // entry point in the EFI-reachable physical address space.
    //
    // Search the conventional 0xF0000-0xFFFFF area first.

    for (uintptr_t address = 0x000F0000;
         address < 0x00100000;
         address += 16)
    {
        if (!memory_equal(
                address,
                "_SM3_",
                5))
        {
            continue;
        }

        uint8_t length =
            read8(address + 0x06);

        if (length < 0x18)
            continue;

        if (!checksum(address, length))
            continue;

        major =
            read8(address + 0x07);

        minor =
            read8(address + 0x08);

        uint64_t table_address =
            read64(address + 0x10);

        uint32_t maximum_size =
            read32(address + 0x0C);

        if (table_address == 0 ||
            maximum_size == 0)
        {
            continue;
        }

        table =
            static_cast<uintptr_t>(
                table_address);

        table_length =
            maximum_size;

        return true;
    }

    return false;
}

// ------------------------------------------------------------
// Public initialization
// ------------------------------------------------------------

bool init()
{
    clear_info();

    uintptr_t table = 0;
    uint32_t table_length = 0;
    uint16_t count = 0;

    uint8_t major = 0;
    uint8_t minor = 0;

    // Prefer SMBIOS 2.x because it provides an explicit
    // structure count and table length.

    if (find_smbios2(
            table,
            table_length,
            count,
            major,
            minor))
    {
        if (!parse_table(
                table,
                table_length,
                count))
        {
            clear_info();
            return false;
        }

        g_info.major_version = major;
        g_info.minor_version = minor;
        g_info.valid = true;

        return true;
    }

    // SMBIOS 3.x fallback.

    if (find_smbios3(
            table,
            table_length,
            major,
            minor))
    {
        // SMBIOS 3 does not store structure_count in the
        // entry point. Parse until Type 127 or table end.

        uintptr_t current = table;
        uintptr_t end =
            table + table_length;

        bool parsed_any = false;

        while (current + 4 <= end)
        {
            uint8_t type =
                read8(current + 0);

            uint8_t length =
                read8(current + 1);

            if (length < 4)
                break;

            parse_structure(current);
            parsed_any = true;

            uintptr_t next =
                structure_end(current);

            if (next <= current ||
                next > end)
            {
                break;
            }

            current = next;

            if (type == 127)
                break;
        }

        if (!parsed_any)
        {
            clear_info();
            return false;
        }

        g_info.major_version = major;
        g_info.minor_version = minor;
        g_info.valid = true;

        return true;
    }

    return false;
}

// ------------------------------------------------------------
// API
// ------------------------------------------------------------

const Info& get_info()
{
    return g_info;
}

bool available()
{
    return g_info.valid;
}

const char* bios_vendor()
{
    return g_info.bios_vendor;
}

const char* bios_version()
{
    return g_info.bios_version;
}

const char* bios_date()
{
    return g_info.bios_date;
}

const char* manufacturer()
{
    return g_info.manufacturer;
}

const char* product()
{
    return g_info.product;
}

const char* version()
{
    return g_info.version;
}

const char* serial()
{
    return g_info.serial;
}

const char* sku()
{
    return g_info.sku;
}

const char* family()
{
    return g_info.family;
}

const char* board_manufacturer()
{
    return g_info.board_manufacturer;
}

const char* board_product()
{
    return g_info.board_product;
}

const char* board_version()
{
    return g_info.board_version;
}

const char* board_serial()
{
    return g_info.board_serial;
}

const char* board_asset_tag()
{
    return g_info.board_asset_tag;
}

uint8_t version_major()
{
    return g_info.major_version;
}

uint8_t version_minor()
{
    return g_info.minor_version;
}

} // namespace blockos::smbios
