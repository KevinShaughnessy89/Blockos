
#pragma once

#include <stdint.h>
#include <stddef.h>

namespace blockos::smbios
{

// ============================================================
// Raw SMBIOS information
// ============================================================
//
// entry_point:
//     Pointer to the complete raw SMBIOS entry-point structure.
//
// dmi_table:
//     Pointer to the raw SMBIOS/DMI structure table.
//
// table_length:
//     Length of the raw DMI/SMBIOS table.
//
// The data is NOT parsed or copied by this interface.
// A consumer such as lazybios may make its own copy.
// ============================================================

struct RawSMBIOS
{
    const void* entry_point;
    const void* dmi_table;
    uint32_t table_length;
};

// ============================================================
// Internal state
// ============================================================

static RawSMBIOS g_raw =
{
    nullptr,
    nullptr,
    0
};

// ============================================================
// Raw memory access
// ============================================================
//
// These functions assume that the specified physical memory
// region is already accessible through the current address space.
//
// If BlockOS uses a higher-half/direct physical mapping, these
// functions should be replaced by the appropriate physical
// address mapping functions.
// ============================================================

static uint8_t read8(uintptr_t address)
{
    return *reinterpret_cast<volatile uint8_t*>(address);
}

static uint16_t read16(uintptr_t address)
{
    return *reinterpret_cast<volatile uint16_t*>(address);
}

static uint32_t read32(uintptr_t address)
{
    return *reinterpret_cast<volatile uint32_t*>(address);
}

static uint64_t read64(uintptr_t address)
{
    return *reinterpret_cast<volatile uint64_t*>(address);
}

// ============================================================
// Compare memory with ASCII signature
// ============================================================

static bool signature_equals(
    uintptr_t address,
    const char* signature,
    size_t length)
{
    for (size_t i = 0; i < length; ++i)
    {
        if (read8(address + i) !=
            static_cast<uint8_t>(signature[i]))
        {
            return false;
        }
    }

    return true;
}

// ============================================================
// Checksum
// ============================================================

static bool valid_checksum(
    uintptr_t address,
    size_t length)
{
    uint8_t sum = 0;

    for (size_t i = 0; i < length; ++i)
    {
        sum = static_cast<uint8_t>(
            sum + read8(address + i)
        );
    }

    return sum == 0;
}

// ============================================================
// Clear previously discovered SMBIOS information
// ============================================================

static void clear()
{
    g_raw.entry_point = nullptr;
    g_raw.dmi_table = nullptr;
    g_raw.table_length = 0;
}

// ============================================================
// SMBIOS 2.x entry point
// ============================================================
//
// Layout:
//
//   00  "_SM_"
//   04  checksum
//   05  length
//   06  major version
//   07  minor version
//   08  maximum structure size
//   0A  entry revision
//   0B  formatted area
//   10  "_DMI_"
//   15  DMI checksum
//   16  table length
//   18  table address
//   1C  structure count
//   1E  BCD revision
//
// ============================================================

static bool find_smbios2()
{
    constexpr uintptr_t SEARCH_START = 0x000F0000;
    constexpr uintptr_t SEARCH_END   = 0x00100000;

    for (uintptr_t address = SEARCH_START;
         address < SEARCH_END;
         address += 16)
    {
        // Primary SMBIOS 2.x signature.
        if (!signature_equals(
                address,
                "_SM_",
                4))
        {
            continue;
        }

        const uint8_t entry_length =
            read8(address + 0x05);

        // SMBIOS 2.x entry point is normally at least 0x1F bytes.
        if (entry_length < 0x1F)
        {
            continue;
        }

        // Validate the complete SMBIOS entry point checksum.
        if (!valid_checksum(
                address,
                entry_length))
        {
            continue;
        }

        // Validate the intermediate DMI anchor.
        if (!signature_equals(
                address + 0x10,
                "_DMI_",
                5))
        {
            continue;
        }

        // The intermediate DMI checksum covers 0x10 onward.
        const uint8_t dmi_entry_length =
            0x0F;

        if (!valid_checksum(
                address + 0x10,
                dmi_entry_length))
        {
            continue;
        }

        const uint16_t table_length =
            read16(address + 0x16);

        const uint32_t table_address =
            read32(address + 0x18);

        // SMBIOS structure count.
        const uint16_t structure_count =
            read16(address + 0x1C);

        if (table_address == 0)
        {
            continue;
        }

        if (table_length == 0)
        {
            continue;
        }

        if (structure_count == 0)
        {
            continue;
        }

        // --------------------------------------------------------
        // IMPORTANT:
        //
        // Do NOT parse the SMBIOS structures here.
        //
        // Return the raw entry point and raw DMI table.
        // --------------------------------------------------------

        g_raw.entry_point =
            reinterpret_cast<const void*>(address);

        g_raw.dmi_table =
            reinterpret_cast<const void*>(
                static_cast<uintptr_t>(table_address)
            );

        g_raw.table_length = table_length;

        return true;
    }

    return false;
}

// ============================================================
// SMBIOS 3.x entry point
// ============================================================
//
// Layout:
//
//   00  "_SM3_"
//   05  checksum
//   06  length
//   07  major version
//   08  minor version
//   09  document revision
//   0A  entry revision
//   0B  reserved
//   0C  table maximum size
//   10  table address
//
// ============================================================

static bool find_smbios3()
{
    constexpr uintptr_t SEARCH_START = 0x000F0000;
    constexpr uintptr_t SEARCH_END   = 0x00100000;

    for (uintptr_t address = SEARCH_START;
         address < SEARCH_END;
         address += 16)
    {
        if (!signature_equals(
                address,
                "_SM3_",
                5))
        {
            continue;
        }

        const uint8_t entry_length =
            read8(address + 0x06);

        // SMBIOS 3.x entry point is 0x18 bytes.
        if (entry_length < 0x18)
        {
            continue;
        }

        if (!valid_checksum(
                address,
                entry_length))
        {
            continue;
        }

        const uint32_t table_max_size =
            read32(address + 0x0C);

        const uint64_t table_address =
            read64(address + 0x10);

        if (table_address == 0)
        {
            continue;
        }

        if (table_max_size == 0)
        {
            continue;
        }

        // --------------------------------------------------------
        // IMPORTANT:
        //
        // Again, no SMBIOS parsing happens here.
        // The raw entry point is exposed to the caller.
        // --------------------------------------------------------

        g_raw.entry_point =
            reinterpret_cast<const void*>(address);

        g_raw.dmi_table =
            reinterpret_cast<const void*>(
                static_cast<uintptr_t>(table_address)
            );

        g_raw.table_length =
            table_max_size;

        return true;
    }

    return false;
}

// ============================================================
// Initialize SMBIOS discovery
// ============================================================
//
// Returns true when a valid SMBIOS entry point was found.
//
// No SMBIOS structures are parsed.
// ============================================================

bool init()
{
    clear();

    // SMBIOS 2.x
    if (find_smbios2())
    {
        return true;
    }

    // SMBIOS 3.x
    if (find_smbios3())
    {
        return true;
    }

    return false;
}

// ============================================================
// Raw API
// ============================================================

const RawSMBIOS* get_raw()
{
    if (g_raw.entry_point == nullptr ||
        g_raw.dmi_table == nullptr ||
        g_raw.table_length == 0)
    {
        return nullptr;
    }

    return &g_raw;
}

// ============================================================
// Individual raw accessors
// ============================================================

const void* get_entry_point()
{
    return g_raw.entry_point;
}

const void* get_dmi_table()
{
    return g_raw.dmi_table;
}

uint32_t get_table_length()
{
    return g_raw.table_length;
}

// ============================================================
// Availability
// ============================================================

bool available()
{
    return
        g_raw.entry_point != nullptr &&
        g_raw.dmi_table != nullptr &&
        g_raw.table_length != 0;
}

} // namespace blockos::smbios
