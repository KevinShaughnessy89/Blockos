#include "power.hpp"
#include "acpi.hpp"

extern "C" {
#include <efi.h>
#include <efilib.h>
}

#include <stdint.h>
#include <stddef.h>

extern "C" bool blockos_storage_flush();

namespace power {
namespace {

#pragma pack(push, 1)
struct Gas {
    uint8_t address_space;
    uint8_t bit_width;
    uint8_t bit_offset;
    uint8_t access_size;
    uint64_t address;
};

struct FadtPrefix {
    acpi::SdtHeader header;
    uint32_t firmware_ctrl;
    uint32_t dsdt;
    uint8_t reserved0;
    uint8_t preferred_profile;
    uint16_t sci_int;
    uint32_t smi_cmd;
    uint8_t acpi_enable;
    uint8_t acpi_disable;
    uint8_t s4bios_req;
    uint8_t pstate_cnt;
    uint32_t pm1a_evt_blk;
    uint32_t pm1b_evt_blk;
    uint32_t pm1a_cnt_blk;
    uint32_t pm1b_cnt_blk;
    uint32_t pm2_cnt_blk;
    uint32_t pm_tmr_blk;
    uint32_t gpe0_blk;
    uint32_t gpe1_blk;
    uint8_t pm1_evt_len;
    uint8_t pm1_cnt_len;
    uint8_t pm2_cnt_len;
    uint8_t pm_tmr_len;
    uint8_t gpe0_blk_len;
    uint8_t gpe1_blk_len;
    uint8_t gpe1_base;
    uint8_t cst_cnt;
    uint16_t plvl2_lat;
    uint16_t plvl3_lat;
    uint16_t flush_size;
    uint16_t flush_stride;
    uint8_t duty_offset;
    uint8_t duty_width;
    uint8_t day_alarm;
    uint8_t mon_alarm;
    uint8_t century;
    uint16_t iapc_boot_arch;
    uint8_t reserved1;
    uint32_t flags;
    Gas reset_reg;
    uint8_t reset_value;
    uint16_t arm_boot_arch;
    uint8_t fadt_minor;
    uint64_t x_firmware_ctrl;
    uint64_t x_dsdt;
    Gas x_pm1a_evt_blk;
    Gas x_pm1b_evt_blk;
    Gas x_pm1a_cnt_blk;
    Gas x_pm1b_cnt_blk;
    Gas x_pm2_cnt_blk;
    Gas x_pm_tmr_blk;
    Gas x_gpe0_blk;
    Gas x_gpe1_blk;
    Gas sleep_control_reg;
    Gas sleep_status_reg;
};
#pragma pack(pop)

struct SleepTypes {
    bool present = false;
    uint8_t s3 = 0;
    uint8_t s4 = 0;
    uint8_t s5 = 0;
};

const FadtPrefix* g_fadt = nullptr;
SleepTypes g_sleep{};
bool g_ready = false;
State g_state = State::Unknown;

static inline void out8(uint16_t port, uint8_t value)
{
    __asm__ volatile("outb %0,%1" : : "a"(value), "dN"(port));
}

static inline uint8_t in8(uint16_t port)
{
    uint8_t value;
    __asm__ volatile("inb %1,%0" : "=a"(value) : "dN"(port));
    return value;
}

static void io_delay()
{
    (void)in8(0x80);
}

static bool find_aml_integer(const uint8_t* data, size_t length, const char name[4], uint8_t* out)
{
    if (!data || !out || length < 8)
        return false;

    for (size_t i = 0; i + 8 < length; ++i) {
        if (data[i] != '_')
            continue;
        if (data[i + 1] != name[1] || data[i + 2] != name[2] || data[i + 3] != name[3])
            continue;

        /* Accept NameOp + four-character name + PackageOp. */
        size_t p = i + 4;
        if (i == 0 || data[i - 1] != 0x08 || p >= length)
            continue;
        if (data[p] != 0x12 || p + 3 >= length)
            continue;

        p += 2; // PackageOp + one-byte package length (sufficient for common _Sx packages)
        if (p >= length)
            continue;
        const uint8_t elements = data[p++];
        if (elements == 0)
            continue;

        auto read_integer = [&](size_t& q, uint8_t& value) -> bool {
            if (q >= length)
                return false;
            switch (data[q]) {
                case 0x0A:
                    if (q + 1 >= length) return false;
                    value = data[q + 1]; q += 2; return true;
                case 0x0B:
                    if (q + 2 >= length) return false;
                    value = data[q + 1]; q += 3; return true;
                case 0x0C:
                    if (q + 4 >= length) return false;
                    value = data[q + 1]; q += 5; return true;
                case 0x00:
                case 0x01:
                    value = data[q]; ++q; return true;
                default:
                    return false;
            }
        };

        uint8_t first = 0;
        if (read_integer(p, first)) {
            *out = first;
            return true;
        }
    }
    return false;
}

static bool parse_sleep_types()
{
    if (!g_fadt)
        return false;

    uint64_t dsdt_addr = g_fadt->x_dsdt ? g_fadt->x_dsdt : g_fadt->dsdt;
    if (!dsdt_addr)
        return false;

    const acpi::SdtHeader* dsdt = reinterpret_cast<const acpi::SdtHeader*>(static_cast<uintptr_t>(dsdt_addr));
    if (dsdt->length < sizeof(acpi::SdtHeader) || !acpi::checksum_ok(dsdt, dsdt->length))
        return false;

    const uint8_t* aml = reinterpret_cast<const uint8_t*>(dsdt) + sizeof(acpi::SdtHeader);
    const size_t aml_len = dsdt->length - sizeof(acpi::SdtHeader);
    g_sleep.present = find_aml_integer(aml, aml_len, "_S3_", &g_sleep.s3) &&
                      find_aml_integer(aml, aml_len, "_S5_", &g_sleep.s5);
    if (!g_sleep.present)
        (void)find_aml_integer(aml, aml_len, "_S5_", &g_sleep.s5);
    (void)find_aml_integer(aml, aml_len, "_S4_", &g_sleep.s4);
    return g_sleep.s3 != 0 || g_sleep.s5 != 0;
}

static bool write_pm1(uint16_t port, uint16_t value)
{
    if (!port)
        return false;
    out8(port, static_cast<uint8_t>(value & 0xFF));
    io_delay();
    out8(static_cast<uint16_t>(port + 1), static_cast<uint8_t>(value >> 8));
    return true;
}

static bool acpi_power_value(uint8_t sleep_type)
{
    if (!g_fadt || !g_fadt->pm1a_cnt_blk || g_fadt->pm1_cnt_len < 2)
        return false;

    const uint16_t value = static_cast<uint16_t>((uint16_t(sleep_type & 0x07) << 10) | (1u << 13));
    if (!write_pm1(static_cast<uint16_t>(g_fadt->pm1a_cnt_blk), value))
        return false;
    if (g_fadt->pm1b_cnt_blk)
        (void)write_pm1(static_cast<uint16_t>(g_fadt->pm1b_cnt_blk), value);
    return true;
}

} // namespace

bool init(EFI_SYSTEM_TABLE* system_table)
{
    (void)system_table;
    /* Kernel already exposes the validated RSDP parser; use UEFI tables directly here. */
    if (!system_table)
        return false;

    EFI_GUID acpi20 = {0x8868e871,0xe4f1,0x11d3,{0xbc,0x22,0x00,0x80,0xc7,0x3c,0x88,0x81}};
    EFI_GUID acpi10 = {0xeb9d2d30,0x2d88,0x11d3,{0x9a,0x16,0x00,0x90,0x27,0x3f,0xc1,0x4d}};
    const acpi::Rsdp* rsdp = nullptr;
    for (UINTN i = 0; i < system_table->NumberOfTableEntries; ++i) {
        EFI_CONFIGURATION_TABLE& entry = system_table->ConfigurationTable[i];
        if (CompareGuid(&entry.VendorGuid, &acpi20) || CompareGuid(&entry.VendorGuid, &acpi10)) {
            rsdp = reinterpret_cast<const acpi::Rsdp*>(entry.VendorTable);
            break;
        }
    }
    if (!rsdp)
        return false;

    g_fadt = reinterpret_cast<const FadtPrefix*>(acpi::find_table(rsdp, "FACP"));
    if (!g_fadt || g_fadt->header.length < offsetof(FadtPrefix, sleep_control_reg))
        return false;

    (void)parse_sleep_types();
    g_ready = g_fadt->pm1a_cnt_blk != 0 && g_fadt->pm1_cnt_len >= 2;
    g_state = g_ready ? State::Ready : State::Unknown;
    return g_ready;
}

bool is_available() { return g_ready; }
State state() { return g_state; }

bool sleep()
{
    if (!g_ready)
        return false;
    (void)blockos_storage_flush();
    if (!g_sleep.s3)
        return false;
    g_state = State::Sleep3;
    return acpi_power_value(g_sleep.s3);
}

bool shutdown()
{
    if (!g_ready)
        return false;
    (void)blockos_storage_flush();
    if (!g_sleep.s5)
        return false;
    g_state = State::Shutdown5;
    return acpi_power_value(g_sleep.s5);
}

void idle()
{
    __asm__ volatile("sti; hlt" ::: "memory");
}

bool battery_available() { return false; }
bool thermal_available() { return false; }

} // namespace power
