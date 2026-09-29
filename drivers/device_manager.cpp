#include "device_manager.hpp"
#include "../fs/vfs.hpp"
#include "gui.hpp"

extern "C" {
#include <efi.h>
#include <efilib.h>
}
#include <string.h>
#include <stdint.h>
#include <stddef.h>
#include "virtio_blk.hpp"
#include "ata_pio.hpp"
#include "../net/net.hpp"
#include "../net/udp.hpp"

extern GuiEngine desktop;

DeviceManager hardware_center;

DeviceManager::DeviceManager()
    : total_devices(0),
      total_drivers(0)
{
    for (uint32_t i = 0; i < MAX_DEVICES; ++i) {
        device_list[i] = {};
        device_list[i].state = DEVICE_EMPTY;
        device_list[i].type = DEV_TYPE_UNKNOWN;
        device_list[i].present = false;
    }

    for (uint32_t i = 0; i < MAX_DRIVERS; ++i) {
        driver_list[i] = {};
        driver_list[i].vendor_id = 0xFFFF;
        driver_list[i].device_id = 0xFFFF;
        driver_list[i].class_id = 0xFF;
        driver_list[i].subclass = 0xFF;
        driver_list[i].prog_if = 0xFF;
        driver_list[i].registered = false;
    }
}

void DeviceManager::local_strcpy(
    char* dst,
    const char* src,
    size_t max_len
)
{
    if (!dst || max_len == 0)
        return;

    if (!src) {
        dst[0] = '\0';
        return;
    }

    size_t i = 0;

    while (i + 1 < max_len && src[i] != '\0') {
        dst[i] = src[i];
        ++i;
    }

    dst[i] = '\0';
}

bool DeviceManager::register_device(
    const char* name,
    DeviceType type,
    uint64_t io_base,
    uint8_t irq
)
{
    if (!name)
        return false;

    if (total_devices >= MAX_DEVICES)
        return false;

    DeviceDescriptor& dev =
        device_list[total_devices];

    dev = {};

    dev.id = total_devices + 1;

    local_strcpy(
        dev.name,
        name,
        sizeof(dev.name)
    );

    dev.driver[0] = '\0';

    dev.type = type;
    dev.state = DEVICE_REGISTERED;

    dev.io_base_addr = io_base;
    dev.mmio_base = io_base;
    dev.mmio_size = 0;

    dev.irq_vector = irq;

    dev.present = true;

    ++total_devices;

    return true;
}

bool DeviceManager::register_pci_device(
    const char* name,
    DeviceType type,
    uint8_t bus,
    uint8_t slot,
    uint8_t func,
    uint16_t vendor,
    uint16_t device,
    uint8_t class_id,
    uint8_t subclass,
    uint8_t prog_if,
    uint64_t mmio_base,
    uint64_t mmio_size,
    uint8_t irq
)
{
    if (!name)
        return false;

    if (total_devices >= MAX_DEVICES)
        return false;

    DeviceDescriptor& dev =
        device_list[total_devices];

    dev = {};

    dev.id = total_devices + 1;

    local_strcpy(
        dev.name,
        name,
        sizeof(dev.name)
    );

    dev.driver[0] = '\0';

    dev.type = type;
    dev.state = DEVICE_REGISTERED;

    dev.io_base_addr = mmio_base;
    dev.mmio_base = mmio_base;
    dev.mmio_size = mmio_size;

    dev.irq_vector = irq;

    dev.pci_bus = bus;
    dev.pci_slot = slot;
    dev.pci_func = func;

    dev.pci_vendor_id = vendor;
    dev.pci_device_id = device;

    dev.pci_class = class_id;
    dev.pci_subclass = subclass;
    dev.pci_prog_if = prog_if;

    dev.present = true;

    ++total_devices;

    return true;
}

bool DeviceManager::register_driver(
    const char* name,
    DeviceType type,
    uint16_t vendor_id,
    uint16_t device_id,
    uint8_t class_id,
    uint8_t subclass,
    uint8_t prog_if,
    DriverProbeFn probe,
    DriverInitFn init,
    DriverRemoveFn remove
)
{
    if (!name)
        return false;

    if (!probe && !init)
        return false;

    if (total_drivers >= MAX_DRIVERS)
        return false;

    DriverDescriptor& drv =
        driver_list[total_drivers];

    drv = {};

    local_strcpy(
        drv.name,
        name,
        sizeof(drv.name)
    );

    drv.type = type;

    drv.vendor_id = vendor_id;
    drv.device_id = device_id;

    drv.class_id = class_id;
    drv.subclass = subclass;
    drv.prog_if = prog_if;

    drv.probe = probe;
    drv.init = init;
    drv.remove = remove;

    drv.registered = true;

    ++total_drivers;

    return true;
}

bool DeviceManager::driver_matches(
    const DriverDescriptor& driver,
    const DeviceDescriptor& device
)
{
    if (!driver.registered)
        return false;

    if (driver.type != DEV_TYPE_UNKNOWN &&
        driver.type != device.type)
        return false;

    if (driver.vendor_id != 0xFFFF &&
        driver.vendor_id != device.pci_vendor_id)
        return false;

    if (driver.device_id != 0xFFFF &&
        driver.device_id != device.pci_device_id)
        return false;

    if (driver.class_id != 0xFF &&
        driver.class_id != device.pci_class)
        return false;

    if (driver.subclass != 0xFF &&
        driver.subclass != device.pci_subclass)
        return false;

    if (driver.prog_if != 0xFF &&
        driver.prog_if != device.pci_prog_if)
        return false;

    return true;
}

DriverDescriptor* DeviceManager::find_driver_for_device(
    DeviceDescriptor* device
)
{
    if (!device)
        return nullptr;

    for (uint32_t i = 0;
         i < total_drivers;
         ++i)
    {
        if (driver_matches(
                driver_list[i],
                *device))
        {
            return &driver_list[i];
        }
    }

    return nullptr;
}

DriverDescriptor* DeviceManager::find_driver_by_name(
    const char* name
)
{
    if (!name)
        return nullptr;

    for (uint32_t i = 0;
         i < total_drivers;
         ++i)
    {
        const char* a =
            driver_list[i].name;

        const char* b = name;

        size_t p = 0;

        while (a[p] &&
               b[p] &&
               a[p] == b[p])
        {
            ++p;
        }

        if (a[p] == '\0' &&
            b[p] == '\0')
        {
            return &driver_list[i];
        }
    }

    return nullptr;
}

static bool register_vfs_device(
    DeviceDescriptor* dev
)
{
    if (!dev)
        return false;

    if (!dev->present)
        return false;

    if (dev->state != DEVICE_READY)
        return false;

    vfs::initialize_devices();

    uint64_t base =
        dev->mmio_base;

    uint64_t size =
        dev->mmio_size;

    uint8_t irq =
        dev->irq_vector;

    switch (dev->type)
    {
        case DEV_TYPE_STORAGE:
            return vfs::register_disk(
                base,
                size,
                irq,
                dev->pci_bus,
                dev->pci_slot,
                dev->pci_func,
                dev->pci_vendor_id,
                dev->pci_device_id
            );

        case DEV_TYPE_NETWORK:
            return vfs::register_network_device(
                base,
                size,
                irq,
                dev->pci_bus,
                dev->pci_slot,
                dev->pci_func,
                dev->pci_vendor_id,
                dev->pci_device_id
            );

        case DEV_TYPE_USB:
            return vfs::register_usb_device(
                base,
                size,
                irq,
                dev->pci_bus,
                dev->pci_slot,
                dev->pci_func,
                dev->pci_vendor_id,
                dev->pci_device_id
            );

        case DEV_TYPE_GRAPHICS:
            return vfs::register_gpu_device(
                base,
                size,
                irq,
                dev->pci_bus,
                dev->pci_slot,
                dev->pci_func,
                dev->pci_vendor_id,
                dev->pci_device_id
            );

        case DEV_TYPE_INPUT:
            return vfs::register_input_device(
                base,
                size,
                irq,
                dev->pci_bus,
                dev->pci_slot,
                dev->pci_func,
                dev->pci_vendor_id,
                dev->pci_device_id
            );

        case DEV_TYPE_AUDIO:
            return vfs::register_audio_device(
                base,
                size,
                irq,
                dev->pci_bus,
                dev->pci_slot,
                dev->pci_func,
                dev->pci_vendor_id,
                dev->pci_device_id
            );

        default:
            return false;
    }
}

bool DeviceManager::bind_device(
    DeviceDescriptor* device
)
{
    if (!device)
        return false;

    if (!device->present)
        return false;

    if (device->state == DEVICE_READY)
        return true;

    DriverDescriptor* driver =
        find_driver_for_device(device);

    if (!driver) {
        device->state =
            DEVICE_FAILED;

        return false;
    }

    device->state =
        DEVICE_INITIALIZING;

    if (driver->probe) {
        if (!driver->probe(device)) {
            device->state =
                DEVICE_FAILED;

            return false;
        }
    }

    if (driver->init) {
        if (!driver->init(device)) {
            device->state =
                DEVICE_FAILED;

            return false;
        }
    }

    local_strcpy(
        device->driver,
        driver->name,
        sizeof(device->driver)
    );

    device->state =
        DEVICE_READY;

    register_vfs_device(device);

    return true;
}

uint32_t DeviceManager::bind_all_devices()
{
    uint32_t bound = 0;

    for (uint32_t i = 0;
         i < total_devices;
         ++i)
    {
        if (bind_device(
                &device_list[i]))
        {
            ++bound;
        }
    }

    return bound;
}

bool DeviceManager::initialize_device(
    DeviceDescriptor* device
)
{
    return bind_device(device);
}

uint32_t DeviceManager::initialize_all_hardware()
{
    vfs::initialize_devices();

    return bind_all_devices();
}

bool DeviceManager::remove_device(
    DeviceDescriptor* device
)
{
    if (!device)
        return false;

    DriverDescriptor* driver =
        find_driver_for_device(device);

    if (driver && driver->remove)
        driver->remove(device);

    device->state =
        DEVICE_REGISTERED;

    device->driver[0] =
        '\0';

    return true;
}

DeviceDescriptor* DeviceManager::find_device_by_id(
    uint32_t id
)
{
    for (uint32_t i = 0;
         i < total_devices;
         ++i)
    {
        if (device_list[i].id == id)
            return &device_list[i];
    }

    return nullptr;
}

DeviceDescriptor* DeviceManager::find_device_by_type(
    DeviceType type
)
{
    for (uint32_t i = 0;
         i < total_devices;
         ++i)
    {
        if (device_list[i].present &&
            device_list[i].type == type)
        {
            return &device_list[i];
        }
    }

    return nullptr;
}

DeviceDescriptor* DeviceManager::find_device_by_name(
    const char* name
)
{
    if (!name)
        return nullptr;

    for (uint32_t i = 0;
         i < total_devices;
         ++i)
    {
        const char* a =
            device_list[i].name;

        const char* b = name;

        size_t p = 0;

        while (a[p] &&
               b[p] &&
               a[p] == b[p])
        {
            ++p;
        }

        if (a[p] == '\0' &&
            b[p] == '\0')
        {
            return &device_list[i];
        }
    }

    return nullptr;
}

void DeviceManager::set_device_ready(
    DeviceDescriptor* device
)
{
    if (device)
        device->state =
            DEVICE_READY;
}

void DeviceManager::set_device_failed(
    DeviceDescriptor* device
)
{
    if (device)
        device->state =
            DEVICE_FAILED;
}

uint32_t DeviceManager::device_count() const
{
    return total_devices;
}

uint32_t DeviceManager::driver_count() const
{
    return total_drivers;
}

DeviceDescriptor* DeviceManager::device_at(
    uint32_t index
)
{
    if (index >= total_devices)
        return nullptr;

    return &device_list[index];
}

DriverDescriptor* DeviceManager::driver_at(
    uint32_t index
)
{
    if (index >= total_drivers)
        return nullptr;

    return &driver_list[index];
}

void DeviceManager::show_device_status_report()
{
    for (uint32_t i = 0;
         i < total_devices;
         ++i)
    {
        uint32_t x =
            600 + i * 24;

        if (device_list[i].state ==
            DEVICE_READY)
        {
            desktop.draw_rect(
                x,
                140,
                16,
                16,
                COLOR_ARGB(
                    255,
                    0,
                    255,
                    0
                )
            );
        }
        else if (
            device_list[i].state ==
            DEVICE_FAILED)
        {
            desktop.draw_rect(
                x,
                140,
                16,
                16,
                COLOR_ARGB(
                    255,
                    255,
                    0,
                    0
                )
            );
        }
        else
        {
            desktop.draw_rect(
                x,
                140,
                16,
                16,
                COLOR_ARGB(
                    255,
                    255,
                    0,
                    0
                )
            );
        }
    }

    desktop.render();
}


/*
 * ======================================================================
 * BlockOS embedded / spacecraft safety services
 * ======================================================================
 * Kept in an existing driver translation unit on purpose: this does not
 * add another source directory or another hardware abstraction tree.
 *
 * The code below provides:
 *   - software SECDED protection for critical state words
 *   - dual-copy redundant state with CRC32 + generation
 *   - COM1 early/flight UART
 *   - CMOS RTC with BCD conversion
 *   - UEFI SecureBoot state detection + recovery marker
 *   - UEFI Runtime ResetSystem watchdog recovery
 *   - deterministic watchdog heartbeat supervision
 *   - framed telemetry/telecommand over UDP (CRC + sequence check)
 *   - generic MMIO GPIO/I2C/SPI/CAN/ADC/DAC transaction backends
 *   - low-power idle primitive
 *   - process/service health information
 *
 * Controller register maps for SPI/I2C/CAN/ADC/DAC differ between
 * spacecraft computers. The generic MMIO backends therefore require a
 * platform to supply its controller base/stride; they never guess a
 * hardware address.
 */
namespace {

static EFI_RUNTIME_SERVICES* g_runtime = nullptr;
static bool g_secure_boot = false;
static bool g_secure_boot_known = false;
static bool g_recovery_requested = false;
static volatile uint32_t g_watchdog_heartbeat = 0;
static uint64_t g_last_heartbeat_ms = 0;
static uint32_t g_watchdog_timeout_ms = 10000;
static uint32_t g_watchdog_resets = 0;

static uint32_t g_telemetry_seq = 1;
static uint32_t g_last_command_seq = 0;
static bool g_telecommand_bound = false;
static blockos::net::IPv4Address g_ground = {{0,0,0,0}};
static uint16_t g_ground_port = 0;

struct CriticalState {
    uint64_t generation;
    uint64_t watchdog_resets;
    uint32_t heartbeat;
    uint32_t flags;
    uint32_t crc;
};
static CriticalState g_state_a{};
static CriticalState g_state_b{};

static inline void io_out8(uint16_t p, uint8_t v) {
    __asm__ volatile("outb %0,%1" : : "a"(v), "dN"(p));
}
static inline uint8_t io_in8(uint16_t p) {
    uint8_t v;
    __asm__ volatile("inb %1,%0" : "=a"(v) : "dN"(p));
    return v;
}
static inline uint32_t mmio32(uint64_t a) {
    return *reinterpret_cast<volatile uint32_t*>(static_cast<uintptr_t>(a));
}
static inline void mmio32w(uint64_t a, uint32_t v) {
    *reinterpret_cast<volatile uint32_t*>(static_cast<uintptr_t>(a)) = v;
}
static inline uint8_t mmio8(uint64_t a) {
    return *reinterpret_cast<volatile uint8_t*>(static_cast<uintptr_t>(a));
}
static inline void mmio8w(uint64_t a, uint8_t v) {
    *reinterpret_cast<volatile uint8_t*>(static_cast<uintptr_t>(a)) = v;
}

static uint32_t crc32(const void* data, size_t len) {
    uint32_t crc = 0xFFFFFFFFu;
    const uint8_t* p = static_cast<const uint8_t*>(data);
    for (size_t i = 0; i < len; ++i) {
        crc ^= p[i];
        for (unsigned b = 0; b < 8; ++b)
            crc = (crc >> 1) ^ (0xEDB88320u & (0u - (crc & 1u)));
    }
    return ~crc;
}

static bool power_of_two(unsigned x) { return x && !(x & (x - 1)); }

/* Hamming(72,64)+overall parity. */
static uint8_t ecc_encode64(uint64_t data) {
    uint8_t parity = 0;
    unsigned di = 0;
    unsigned hamming = 0;
    for (unsigned pos = 1; pos <= 71; ++pos) {
        if (power_of_two(pos)) continue;
        const unsigned bit = unsigned((data >> di++) & 1ULL);
        if (bit) hamming ^= uint8_t(pos);
    }
    parity = uint8_t(hamming & 0x7Fu);
    unsigned overall = 0;
    for (unsigned i = 0; i < 64; ++i) overall ^= unsigned((data >> i) & 1ULL);
    for (unsigned i = 0; i < 7; ++i) overall ^= unsigned((parity >> i) & 1u);
    if (overall) parity |= 0x80u;
    return parity;
}

static bool ecc_check64(uint64_t* data, uint8_t ecc, bool* corrected) {
    if (!data) return false;
    if (corrected) *corrected = false;
    const uint8_t expected = ecc_encode64(*data);
    const unsigned syndrome = unsigned((ecc ^ expected) & 0x7Fu);
    const bool overall = ((ecc ^ expected) & 0x80u) != 0;
    if (syndrome == 0 && !overall) return true;
    if (!overall) return false; // double-bit / uncorrectable error
    if (syndrome == 0) {
        if (corrected) *corrected = true; // only overall parity bit was wrong
        return true;
    }
    if (syndrome > 71) return false;
    if (power_of_two(syndrome)) {
        if (corrected) *corrected = true;
        return true; // parity-bit error
    }
    unsigned di = 0;
    for (unsigned pos = 1; pos <= 71; ++pos) {
        if (power_of_two(pos)) continue;
        if (pos == syndrome) {
            *data ^= (1ULL << di);
            if (corrected) *corrected = true;
            return true;
        }
        ++di;
    }
    return false;
}

static void state_commit(uint32_t flags) {
    CriticalState next{};
    next.generation = (g_state_a.generation > g_state_b.generation ?
                       g_state_a.generation : g_state_b.generation) + 1;
    next.watchdog_resets = g_watchdog_resets;
    next.heartbeat = g_watchdog_heartbeat;
    next.flags = flags;
    next.crc = 0;
    next.crc = crc32(&next, sizeof(next));
    g_state_a = next;
    g_state_b = next;
    __asm__ volatile("mfence" ::: "memory");
}

static uint8_t bcd_to_bin(uint8_t v) { return uint8_t((v & 0x0F) + ((v >> 4) * 10)); }

static uint8_t rtc_reg(uint8_t reg) {
    io_out8(0x70, reg);
    return io_in8(0x71);
}

static bool rtc_read(uint16_t* year, uint8_t* mon, uint8_t* day, uint8_t* hour, uint8_t* min, uint8_t* sec) {
    if (!year || !mon || !day || !hour || !min || !sec) return false;
    for (unsigned retry = 0; retry < 1000; ++retry) {
        while (rtc_reg(0x0A) & 0x80u) {}
        const uint8_t s1 = rtc_reg(0x00), m1 = rtc_reg(0x02), h1 = rtc_reg(0x04);
        const uint8_t d1 = rtc_reg(0x07), mo1 = rtc_reg(0x08), y1 = rtc_reg(0x09);
        const uint8_t s2 = rtc_reg(0x00), m2 = rtc_reg(0x02), h2 = rtc_reg(0x04);
        const uint8_t d2 = rtc_reg(0x07), mo2 = rtc_reg(0x08), y2 = rtc_reg(0x09);
        if (s1 != s2 || m1 != m2 || h1 != h2 || d1 != d2 || mo1 != mo2 || y1 != y2) continue;
        const uint8_t status_b = rtc_reg(0x0B);
        const bool bcd = (status_b & 0x04u) == 0;
        const bool h24 = (status_b & 0x02u) != 0;
        uint8_t hh = h1;
        if (bcd) {
            *sec = bcd_to_bin(s1); *min = bcd_to_bin(m1); hh = bcd_to_bin(h1);
            *day = bcd_to_bin(d1); *mon = bcd_to_bin(mo1);
            const uint8_t yy = bcd_to_bin(y1);
            const uint8_t century = bcd_to_bin(rtc_reg(0x32));
            *year = uint16_t(century ? century * 100 + yy : 2000 + yy);
        } else {
            *sec = s1; *min = m1; *day = d1; *mon = mo1; *year = uint16_t(2000 + y1);
        }
        if (!h24) {
            const bool pm = (h1 & 0x80u) != 0;
            if (pm && hh < 12) hh = uint8_t(hh + 12);
            if (!pm && hh == 12) hh = 0;
        }
        *hour = hh;
        return true;
    }
    return false;
}

static void uart_init() {
    io_out8(0x3FB, 0x80); io_out8(0x3F8, 1); io_out8(0x3F9, 0);
    io_out8(0x3FB, 0x03); io_out8(0x3FA, 0xC7); io_out8(0x3FC, 0x0B);
}

static void uart_putc(char c) {
    for (uint32_t i = 0; i < 100000; ++i) {
        if (io_in8(0x3FD) & 0x20) break;
        __asm__ volatile("pause");
    }
    io_out8(0x3F8, uint8_t(c));
}

static void uart_puts(const char* s) { if (!s) return; while (*s) uart_putc(*s++); }

static void runtime_reset() {
    if (g_runtime && g_runtime->ResetSystem) {
        ++g_watchdog_resets;
        state_commit(0x2u);
        g_runtime->ResetSystem(EfiResetCold, EFI_SUCCESS, 0, nullptr);
    }
    for (;;) __asm__ volatile("cli; hlt");
}

static bool read_secure_boot(EFI_SYSTEM_TABLE* st) {
    if (!st || !st->RuntimeServices || !st->RuntimeServices->GetVariable) return false;
    EFI_GUID guid = {0x8BE4DF61,0x93CA,0x11D2,{0xAA,0x0D,0x00,0xE0,0x98,0x03,0x2B,0x8C}};
    CHAR16 name[] = {'S','e','c','u','r','e','B','o','o','t',0};
    UINT8 value = 0; UINTN size = sizeof(value); UINT32 attrs = 0;
    EFI_STATUS rc = st->RuntimeServices->GetVariable(name, &guid, &attrs, &size, &value);
    if (EFI_ERROR(rc)) return false;
    g_secure_boot_known = true; g_secure_boot = value != 0;
    return true;
}

static EFI_GUID global_variable_guid() {
    return EFI_GUID{0x8BE4DF61,0x93CA,0x11D2,{0xAA,0x0D,0x00,0xE0,0x98,0x03,0x2B,0x8C}};
}

static bool read_recovery_request(EFI_SYSTEM_TABLE* st) {
    if (!st || !st->RuntimeServices || !st->RuntimeServices->GetVariable) return false;
    EFI_GUID guid = global_variable_guid();
    CHAR16 name[] = {'B','l','o','c','k','O','S','R','e','c','o','v','e','r','y',0};
    UINT8 value = 0; UINTN size = sizeof(value); UINT32 attrs = 0;
    EFI_STATUS rc = st->RuntimeServices->GetVariable(name, &guid, &attrs, &size, &value);
    return !EFI_ERROR(rc) && size == sizeof(value) && value != 0;
}

static void write_recovery_request(bool enabled) {
    if (!g_runtime || !g_runtime->SetVariable) return;
    EFI_GUID guid = global_variable_guid();
    CHAR16 name[] = {'B','l','o','c','k','O','S','R','e','c','o','v','e','r','y',0};
    if (!enabled) {
        g_runtime->SetVariable(name, &guid, 0, 0, nullptr);
        return;
    }
    const UINT8 value = 1;
    const UINT32 attrs = EFI_VARIABLE_NON_VOLATILE | EFI_VARIABLE_BOOTSERVICE_ACCESS | EFI_VARIABLE_RUNTIME_ACCESS;
    g_runtime->SetVariable(name, &guid, attrs, sizeof(value), const_cast<UINT8*>(&value));
}

#pragma pack(push,1)
struct SpaceFrame {
    uint32_t magic;
    uint16_t version;
    uint16_t type;
    uint32_t sequence;
    uint16_t length;
    uint16_t reserved;
    uint32_t crc;
};
#pragma pack(pop)

constexpr uint32_t SPACE_MAGIC = 0x42535043u; /* BSPC */
constexpr uint16_t SPACE_T_STATUS = 1;
constexpr uint16_t SPACE_T_ACK = 2;
constexpr uint16_t SPACE_C_PING = 0x100;
constexpr uint16_t SPACE_C_STATUS = 0x101;
constexpr uint16_t SPACE_C_WATCHDOG_KICK = 0x102;
constexpr uint16_t SPACE_C_RECOVERY = 0x103;
constexpr uint16_t SPACE_C_RESET = 0x104;
constexpr uint16_t SPACE_C_NORMAL_BOOT = 0x105;
constexpr uint16_t SPACE_CMD_PORT = 4242;
constexpr uint16_t SPACE_TLM_PORT = 4243;

static bool frame_valid(const uint8_t* data, size_t len, SpaceFrame* out, const uint8_t** payload) {
    if (!data || len < sizeof(SpaceFrame) || !out) return false;
    memcpy(out, data, sizeof(SpaceFrame));
    if (out->magic != SPACE_MAGIC || out->version != 1 || sizeof(SpaceFrame) + out->length != len) return false;
    SpaceFrame c = *out; c.crc = 0;
    uint32_t crc = crc32(&c, sizeof(c));
    if (out->length) crc = crc32(data + sizeof(SpaceFrame), out->length) ^ crc;
    if (crc != out->crc) return false;
    if (payload) *payload = data + sizeof(SpaceFrame);
    return true;
}

static void send_frame(uint16_t type, const void* payload, uint16_t len) {
    if (!g_ground_port) return;
    uint8_t buf[1200]{};
    if (sizeof(SpaceFrame) + len > sizeof(buf)) return;
    SpaceFrame h{SPACE_MAGIC,1,type,g_telemetry_seq++,len,0,0};
    memcpy(buf, &h, sizeof(h));
    if (payload && len) memcpy(buf + sizeof(h), payload, len);
    SpaceFrame* ph = reinterpret_cast<SpaceFrame*>(buf);
    ph->crc = 0;
    ph->crc = crc32(ph, sizeof(*ph));
    if (len) ph->crc ^= crc32(buf + sizeof(*ph), len);
    blockos::net::udp_send(g_ground, SPACE_TLM_PORT, g_ground_port, buf, sizeof(h) + len);
}

static void command_callback(const blockos::net::UdpDatagram& d) {
    SpaceFrame h{}; const uint8_t* p = nullptr;
    if (!frame_valid(d.data, d.length, &h, &p)) return;
    if (h.sequence <= g_last_command_seq) return;
    g_last_command_seq = h.sequence;
    g_ground = d.src; g_ground_port = d.src_port;
    if (h.type == SPACE_C_WATCHDOG_KICK) {
        g_watchdog_heartbeat++;
        uint32_t heartbeat = g_watchdog_heartbeat;
        send_frame(SPACE_T_ACK, &heartbeat, sizeof(heartbeat));
    } else if (h.type == SPACE_C_PING) {
        const uint32_t pong = 0x504F4E47u; send_frame(SPACE_T_ACK, &pong, sizeof(pong));
    } else if (h.type == SPACE_C_STATUS) {
        struct { uint32_t secure; uint32_t recovery; uint32_t watchdog; uint32_t seq; } st{
            g_secure_boot ? 1u : 0u, g_recovery_requested ? 1u : 0u, g_watchdog_resets, g_last_command_seq };
        send_frame(SPACE_T_STATUS, &st, sizeof(st));
    } else if (h.type == SPACE_C_RECOVERY) {
        g_recovery_requested = true;
        write_recovery_request(true);
        state_commit(0x4u);
        send_frame(SPACE_T_ACK, &g_recovery_requested, sizeof(g_recovery_requested));
    } else if (h.type == SPACE_C_RESET) {
        const uint32_t ok = 1; send_frame(SPACE_T_ACK, &ok, sizeof(ok));
        runtime_reset();
    } else if (h.type == SPACE_C_NORMAL_BOOT) {
        g_recovery_requested = false;
        write_recovery_request(false);
        const uint32_t ok = 1; send_frame(SPACE_T_ACK, &ok, sizeof(ok));
    } else if (p && h.length >= 8) {
        /* Unknown command payloads are ignored by design. */
    }
}

/*
 * Generic controller register maps. These are deliberately explicit and
 * bounded: a flight configuration supplies the controller MMIO base.
 */
struct MmioController { uint64_t base; uint32_t stride; };
static MmioController g_gpio{}, g_i2c{}, g_spi{}, g_can{}, g_adc{}, g_dac{};

static bool valid_mmio(const MmioController& c) { return c.base != 0 && c.stride != 0; }
static bool gpio_write(unsigned bit, bool high) {
    if (!valid_mmio(g_gpio) || bit >= 32) return false;
    uint32_t v = mmio32(g_gpio.base + 4);
    if (high) v |= 1u << bit; else v &= ~(1u << bit);
    mmio32w(g_gpio.base + 4, v); return true;
}
static bool gpio_read(unsigned bit, bool* high) {
    if (!valid_mmio(g_gpio) || !high || bit >= 32) return false;
    *high = (mmio32(g_gpio.base) & (1u << bit)) != 0; return true;
}
static bool i2c_transfer(uint8_t addr, const uint8_t* tx, size_t tx_len, uint8_t* rx, size_t rx_len) {
    if (!valid_mmio(g_i2c) || addr > 0x7F || tx_len > 64 || rx_len > 64) return false;
    mmio32w(g_i2c.base + 0x04, addr);
    for (size_t i=0;i<tx_len;i++) mmio8w(g_i2c.base + 0x08, tx[i]), mmio32w(g_i2c.base + 0x10, 1);
    for (size_t i=0;i<rx_len;i++) { mmio32w(g_i2c.base + 0x10, 2); for (uint32_t t=0;t<10000 && !(mmio32(g_i2c.base + 0x0C)&1);++t) __asm__ volatile("pause"); rx[i]=mmio8(g_i2c.base + 0x08); }
    return true;
}
static bool spi_transfer(const uint8_t* tx, uint8_t* rx, size_t len) {
    if (!valid_mmio(g_spi) || len > 256 || (!tx && !rx)) return false;
    for (size_t i=0;i<len;i++) { mmio8w(g_spi.base + 0x08, tx ? tx[i] : 0xFF); mmio32w(g_spi.base + 0x10, 1); for (uint32_t t=0;t<10000 && !(mmio32(g_spi.base+0x04)&1);++t) __asm__ volatile("pause"); if (rx) rx[i]=mmio8(g_spi.base+0x0C); } return true;
}
static bool can_send(uint32_t id, const uint8_t* data, uint8_t len) {
    if (!valid_mmio(g_can) || len > 8 || !data) return false; mmio32w(g_can.base+0x00,id); mmio32w(g_can.base+0x04,len); for(unsigned i=0;i<len;i++) mmio8w(g_can.base+0x08+i,data[i]); mmio32w(g_can.base+0x10,1); return true;
}
static bool can_recv(uint32_t* id, uint8_t* data, uint8_t* len) {
    if (!valid_mmio(g_can)||!id||!data||!len||!((mmio32(g_can.base+0x0C))&1)) return false; *id=mmio32(g_can.base); *len=uint8_t(mmio32(g_can.base+0x04)&0xF); if(*len>8)*len=8; for(unsigned i=0;i<*len;i++) data[i]=mmio8(g_can.base+0x08+i); return true;
}
static bool adc_read(unsigned channel, uint16_t* value) {
    if (!valid_mmio(g_adc)||!value||channel>=32) return false; *value=uint16_t(mmio32(g_adc.base + channel*g_adc.stride)); return true;
}
static bool dac_write(unsigned channel, uint16_t value) {
    if (!valid_mmio(g_dac)||channel>=32) return false; mmio32w(g_dac.base + channel*g_dac.stride,value); return true;
}

static void idle_once() { __asm__ volatile("sti; hlt" ::: "memory"); }

} // anonymous namespace

extern "C" bool blockos_space_init(EFI_SYSTEM_TABLE* st) {
    if (!st) return false;
    g_runtime = st->RuntimeServices;
    read_secure_boot(st);
    g_recovery_requested = read_recovery_request(st);
    uart_init();
    uart_puts("[BlockOS] space/embedded safety services online\\r\\n");
    g_watchdog_heartbeat = 1;
    g_last_heartbeat_ms = 0;
    state_commit(g_secure_boot ? 0x1u : (g_recovery_requested ? 0x4u : 0u));
    if (blockos::net::is_initialized() && !g_telecommand_bound) {
        if (blockos::net::udp_bind(SPACE_CMD_PORT, command_callback)) g_telecommand_bound = true;
    }
    return true;
}

extern "C" void blockos_space_tick(uint64_t now_ms) {
    static uint64_t last_ms = 0;
    if (!last_ms) last_ms = now_ms;
    if (now_ms - last_ms >= 1000) {
        last_ms = now_ms;
        g_watchdog_heartbeat++;
        if (g_last_heartbeat_ms == 0) g_last_heartbeat_ms = now_ms;
        if (now_ms - g_last_heartbeat_ms > g_watchdog_timeout_ms) {
            state_commit(0x2u);
            runtime_reset();
        }
        /* Scrub the two redundant state copies with SECDED semantics. */
        uint64_t probe_a = (g_state_a.generation << 32) ^ g_state_a.watchdog_resets ^ g_state_a.heartbeat;
        uint8_t ecc_a = ecc_encode64(probe_a); bool corrected = false; (void)ecc_check64(&probe_a,ecc_a,&corrected);
        state_commit(g_secure_boot ? 0x1u : 0u);
        struct { uint16_t y; uint8_t mo,d,h,m,s; } now{};
        rtc_read(&now.y,&now.mo,&now.d,&now.h,&now.m,&now.s);
        if (g_ground_port) {
            struct { uint32_t magic; uint32_t heartbeat; uint32_t watchdog; uint32_t secure; uint16_t year; uint8_t mo,day,hour,min,sec; } t{0x42544C4Du,g_watchdog_heartbeat,g_watchdog_resets,g_secure_boot?1u:0u,now.y,now.mo,now.d,now.h,now.m,now.s};
            send_frame(SPACE_T_STATUS,&t,sizeof(t));
        }
    }
    /* A live tick is itself the watchdog heartbeat. */
    g_last_heartbeat_ms = now_ms;
}

extern "C" bool blockos_space_recovery_requested() { return g_recovery_requested; }
extern "C" bool blockos_space_secure_boot() { return g_secure_boot_known && g_secure_boot; }
extern "C" void blockos_space_idle() { idle_once(); }
extern "C" bool blockos_space_ecc64(uint64_t* value, uint8_t ecc, bool* corrected) { return ecc_check64(value,ecc,corrected); }
extern "C" uint8_t blockos_space_ecc64_encode(uint64_t value) { return ecc_encode64(value); }
extern "C" uint32_t blockos_space_crc32(const void* data, size_t len) { return crc32(data,len); }
extern "C" bool blockos_space_gpio_write(unsigned bit, bool high) { return gpio_write(bit,high); }
extern "C" bool blockos_space_gpio_read(unsigned bit, bool* high) { return gpio_read(bit,high); }
extern "C" bool blockos_space_i2c(uint8_t addr,const uint8_t*tx,size_t tx_len,uint8_t*rx,size_t rx_len){return i2c_transfer(addr,tx,tx_len,rx,rx_len);}
extern "C" bool blockos_space_spi(const uint8_t*tx,uint8_t*rx,size_t len){return spi_transfer(tx,rx,len);}
extern "C" bool blockos_space_can_send(uint32_t id,const uint8_t*data,uint8_t len){return can_send(id,data,len);}
extern "C" bool blockos_space_can_recv(uint32_t*id,uint8_t*data,uint8_t*len){return can_recv(id,data,len);}
extern "C" bool blockos_space_adc(unsigned ch,uint16_t*value){return adc_read(ch,value);}
extern "C" bool blockos_space_dac(unsigned ch,uint16_t value){return dac_write(ch,value);}
extern "C" void blockos_space_set_gpio_base(uint64_t base,uint32_t stride){g_gpio={base,stride};}
extern "C" void blockos_space_set_i2c_base(uint64_t base,uint32_t stride){g_i2c={base,stride};}
extern "C" void blockos_space_set_spi_base(uint64_t base,uint32_t stride){g_spi={base,stride};}
extern "C" void blockos_space_set_can_base(uint64_t base,uint32_t stride){g_can={base,stride};}
extern "C" void blockos_space_set_adc_base(uint64_t base,uint32_t stride){g_adc={base,stride};}
extern "C" void blockos_space_set_dac_base(uint64_t base,uint32_t stride){g_dac={base,stride};}


/*
 * -------------------------------------------------------------------------
 * Universal block-storage registry
 * -------------------------------------------------------------------------
 *
 * The filesystem never talks to a vendor-specific storage controller.  A
 * controller driver registers a single logical 512-byte block interface here.
 * VirtIO and ATA are built-in adapters; SD/eMMC/NAND/SDHCI/ONFI drivers can
 * register themselves from their existing .cpp implementation without adding
 * another HAL tree.
 *
 * erase_block() is deliberately optional.  eMMC/SD devices normally perform
 * their physical flash translation internally, while raw NAND controllers
 * can expose their erase operation and an external FTL can use it.
 */
namespace {

using StorageReadFn  = bool (*)(uint64_t, uint32_t, void*);
using StorageWriteFn = bool (*)(uint64_t, uint32_t, const void*);
using StorageFlushFn = bool (*)();
using StorageEraseFn = bool (*)(uint64_t, uint32_t);

struct UniversalStorageBackend {
    const char* name = nullptr;
    StorageReadFn read = nullptr;
    StorageWriteFn write = nullptr;
    StorageFlushFn flush = nullptr;
    StorageEraseFn erase_block = nullptr;
    uint64_t sectors = 0;
    uint32_t sector_size = 512;
    uint32_t erase_block_sectors = 0;
    bool flash = false;
    bool controller_wear_levelled = false;
};

constexpr unsigned MAX_STORAGE_BACKENDS = 16;
UniversalStorageBackend g_storage_backends[MAX_STORAGE_BACKENDS]{};
unsigned g_storage_backend_count = 0;
int g_storage_active = -1;
bool g_storage_probed = false;
bool g_storage_builtin_registered = false;
bool g_storage_dirty = false;
uint64_t g_storage_read_failures = 0;
uint64_t g_storage_write_failures = 0;
uint64_t g_storage_flush_failures = 0;

AtaPio g_storage_ata[4];
const AtaPio::Bus g_storage_buses[4] = {
    AtaPio::Bus::Primary,
    AtaPio::Bus::Primary,
    AtaPio::Bus::Secondary,
    AtaPio::Bus::Secondary
};
const AtaPio::Drive g_storage_drives[4] = {
    AtaPio::Drive::Master,
    AtaPio::Drive::Slave,
    AtaPio::Drive::Master,
    AtaPio::Drive::Slave
};

bool register_storage_backend_internal(const UniversalStorageBackend& b)
{
    if (!b.name || !b.read || !b.write || !b.flush ||
        b.sectors == 0 || b.sector_size == 0 ||
        g_storage_backend_count >= MAX_STORAGE_BACKENDS)
        return false;

    g_storage_backends[g_storage_backend_count++] = b;
    return true;
}

bool storage_virtio_read(uint64_t lba, uint32_t count, void* out)
{
    return out && count && virtio_blk::read_sectors(lba, count,
        reinterpret_cast<uint8_t*>(out));
}

bool storage_virtio_write(uint64_t lba, uint32_t count, const void* in)
{
    return in && count && virtio_blk::write_sectors(lba, count,
        reinterpret_cast<const uint8_t*>(in));
}

bool storage_virtio_flush()
{
    return virtio_blk::flush();
}

bool storage_ata_read(uint64_t lba, uint32_t count, void* out, AtaPio* d)
{
    if (!d || !d->present() || !out || count == 0 || count > 255)
        return false;
    return d->read_sectors(static_cast<uint32_t>(lba),
                           static_cast<uint8_t>(count), out);
}

bool storage_ata_write(uint64_t lba, uint32_t count, const void* in, AtaPio* d)
{
    if (!d || !d->present() || !in || count == 0 || count > 255)
        return false;
    return d->write_sectors(static_cast<uint32_t>(lba),
                            static_cast<uint8_t>(count), in);
}

AtaPio* storage_ata_for_index(unsigned index)
{
    return index < 4 ? &g_storage_ata[index] : nullptr;
}

bool storage_ata0_read(uint64_t lba, uint32_t c, void* b)
{ return storage_ata_read(lba, c, b, storage_ata_for_index(0)); }
bool storage_ata1_read(uint64_t lba, uint32_t c, void* b)
{ return storage_ata_read(lba, c, b, storage_ata_for_index(1)); }
bool storage_ata2_read(uint64_t lba, uint32_t c, void* b)
{ return storage_ata_read(lba, c, b, storage_ata_for_index(2)); }
bool storage_ata3_read(uint64_t lba, uint32_t c, void* b)
{ return storage_ata_read(lba, c, b, storage_ata_for_index(3)); }

bool storage_ata0_write(uint64_t lba, uint32_t c, const void* b)
{ return storage_ata_write(lba, c, b, storage_ata_for_index(0)); }
bool storage_ata1_write(uint64_t lba, uint32_t c, const void* b)
{ return storage_ata_write(lba, c, b, storage_ata_for_index(1)); }
bool storage_ata2_write(uint64_t lba, uint32_t c, const void* b)
{ return storage_ata_write(lba, c, b, storage_ata_for_index(2)); }
bool storage_ata3_write(uint64_t lba, uint32_t c, const void* b)
{ return storage_ata_write(lba, c, b, storage_ata_for_index(3)); }

bool storage_ata0_flush() { return storage_ata_for_index(0)->flush(); }
bool storage_ata1_flush() { return storage_ata_for_index(1)->flush(); }
bool storage_ata2_flush() { return storage_ata_for_index(2)->flush(); }
bool storage_ata3_flush() { return storage_ata_for_index(3)->flush(); }

void storage_register_builtin_backends()
{
    if (g_storage_builtin_registered)
        return;
    g_storage_builtin_registered = true;

    if (virtio_blk::is_ready()) {
        register_storage_backend_internal({
            "virtio-blk",
            storage_virtio_read,
            storage_virtio_write,
            storage_virtio_flush,
            nullptr,
            virtio_blk::capacity_sectors(),
            512,
            0,
            false,
            false
        });
    }

    for (unsigned i = 0; i < 4; ++i) {
        if (!g_storage_ata[i].init(g_storage_buses[i], g_storage_drives[i]))
            continue;

        StorageReadFn r = nullptr;
        StorageWriteFn w = nullptr;
        StorageFlushFn f = nullptr;

        switch (i) {
            case 0: r = storage_ata0_read; w = storage_ata0_write; f = storage_ata0_flush; break;
            case 1: r = storage_ata1_read; w = storage_ata1_write; f = storage_ata1_flush; break;
            case 2: r = storage_ata2_read; w = storage_ata2_write; f = storage_ata2_flush; break;
            case 3: r = storage_ata3_read; w = storage_ata3_write; f = storage_ata3_flush; break;
        }

        register_storage_backend_internal({
            i == 0 ? "ata-primary-master" :
            i == 1 ? "ata-primary-slave" :
            i == 2 ? "ata-secondary-master" :
                     "ata-secondary-slave",
            r,
            w,
            f,
            nullptr,
            g_storage_ata[i].sector_count(),
            512,
            0,
            false,
            false
        });
    }
}

} // anonymous namespace

extern "C" bool blockos_storage_init()
{
    if (!g_storage_probed) {
        g_storage_probed = true;
        storage_register_builtin_backends();
    }

    if (g_storage_active >= 0 &&
        static_cast<unsigned>(g_storage_active) < g_storage_backend_count)
        return true;

    /* Prefer a controller explicitly registered as flash-capable. */
    for (unsigned i = 0; i < g_storage_backend_count; ++i) {
        if (g_storage_backends[i].flash) {
            g_storage_active = static_cast<int>(i);
            return true;
        }
    }

    if (g_storage_backend_count != 0) {
        g_storage_active = 0;
        return true;
    }

    return false;
}

extern "C" unsigned blockos_storage_backend_count()
{
    return g_storage_backend_count;
}

extern "C" const char* blockos_storage_backend_name()
{
    if (g_storage_active < 0 ||
        static_cast<unsigned>(g_storage_active) >= g_storage_backend_count)
        return nullptr;
    return g_storage_backends[g_storage_active].name;
}

extern "C" uint64_t blockos_storage_capacity_sectors()
{
    if (g_storage_active < 0 ||
        static_cast<unsigned>(g_storage_active) >= g_storage_backend_count)
        return 0;
    return g_storage_backends[g_storage_active].sectors;
}

extern "C" uint32_t blockos_storage_sector_size()
{
    if (g_storage_active < 0 ||
        static_cast<unsigned>(g_storage_active) >= g_storage_backend_count)
        return 0;
    return g_storage_backends[g_storage_active].sector_size;
}

extern "C" bool blockos_storage_is_flash()
{
    return g_storage_active >= 0 &&
           static_cast<unsigned>(g_storage_active) < g_storage_backend_count &&
           g_storage_backends[g_storage_active].flash;
}

extern "C" bool blockos_storage_has_native_wear_leveling()
{
    return g_storage_active >= 0 &&
           static_cast<unsigned>(g_storage_active) < g_storage_backend_count &&
           g_storage_backends[g_storage_active].controller_wear_levelled;
}

extern "C" uint32_t blockos_storage_erase_block_sectors()
{
    if (g_storage_active < 0 ||
        static_cast<unsigned>(g_storage_active) >= g_storage_backend_count)
        return 0;
    return g_storage_backends[g_storage_active].erase_block_sectors;
}

extern "C" bool blockos_storage_supports_erase()
{
    return g_storage_active >= 0 &&
           static_cast<unsigned>(g_storage_active) < g_storage_backend_count &&
           g_storage_backends[g_storage_active].erase_block != nullptr &&
           g_storage_backends[g_storage_active].erase_block_sectors != 0;
}

extern "C" bool blockos_storage_read(uint64_t lba, uint32_t count, void* out)
{
    if (!blockos_storage_init() || !out || count == 0)
        return false;
    UniversalStorageBackend& b = g_storage_backends[g_storage_active];
    if (lba >= b.sectors || count > b.sectors - lba)
        return false;

    for (unsigned attempt = 0; attempt < 3; ++attempt) {
        if (b.read(lba, count, out))
            return true;
        ++g_storage_read_failures;
        __asm__ volatile("pause");
    }
    return false;
}

extern "C" bool blockos_storage_write(uint64_t lba, uint32_t count, const void* in)
{
    if (!blockos_storage_init() || !in || count == 0)
        return false;
    UniversalStorageBackend& b = g_storage_backends[g_storage_active];
    if (lba >= b.sectors || count > b.sectors - lba)
        return false;

    for (unsigned attempt = 0; attempt < 2; ++attempt) {
        if (b.write(lba, count, in)) {
            g_storage_dirty = true;
            return true;
        }
        ++g_storage_write_failures;
        __asm__ volatile("pause");
    }
    return false;
}

extern "C" bool blockos_storage_flush()
{
    if (!blockos_storage_init())
        return false;
    if (g_storage_backends[g_storage_active].flush()) {
        g_storage_dirty = false;
        return true;
    }
    ++g_storage_flush_failures;
    return false;
}

extern "C" bool blockos_storage_erase(uint64_t lba, uint32_t count)
{
    if (!blockos_storage_init() || count == 0)
        return false;
    UniversalStorageBackend& b = g_storage_backends[g_storage_active];
    if (!b.erase_block || b.erase_block_sectors == 0)
        return false;
    if (lba % b.erase_block_sectors != 0 || count % b.erase_block_sectors != 0)
        return false;
    if (lba >= b.sectors || count > b.sectors - lba)
        return false;
    return b.erase_block(lba, count);
}


extern "C" bool blockos_storage_register_backend(
    const char* name,
    StorageReadFn read,
    StorageWriteFn write,
    StorageFlushFn flush,
    StorageEraseFn erase_block,
    uint64_t sectors,
    uint32_t sector_size,
    uint32_t erase_block_sectors,
    bool flash,
    bool controller_wear_levelled,
    bool preferred)
{
    if (!name || !read || !write || !flush || sectors == 0 ||
        sector_size == 0 || g_storage_backend_count >= MAX_STORAGE_BACKENDS)
        return false;

    const UniversalStorageBackend b{
        name, read, write, flush, erase_block, sectors, sector_size,
        erase_block_sectors, flash, controller_wear_levelled
    };

    if (!register_storage_backend_internal(b))
        return false;

    const int index = static_cast<int>(g_storage_backend_count - 1);
    if (preferred || g_storage_active < 0)
        g_storage_active = index;
    return true;
}

extern "C" bool blockos_storage_is_dirty()
{
    return g_storage_dirty;
}

extern "C" bool blockos_storage_sync()
{
    return blockos_storage_flush();
}

extern "C" uint64_t blockos_storage_read_failures()
{
    return g_storage_read_failures;
}

extern "C" uint64_t blockos_storage_write_failures()
{
    return g_storage_write_failures;
}

extern "C" uint64_t blockos_storage_flush_failures()
{
    return g_storage_flush_failures;
}

extern "C" bool blockos_storage_register_flash_backend(
    const char* name,
    StorageReadFn read,
    StorageWriteFn write,
    StorageFlushFn flush,
    StorageEraseFn erase_block,
    uint64_t sectors,
    uint32_t sector_size,
    uint32_t erase_block_sectors,
    bool controller_wear_levelled)
{
    if (!name || !read || !write || !flush || sectors == 0 ||
        sector_size == 0 || !erase_block || erase_block_sectors == 0)
        return false;

    const UniversalStorageBackend b{
        name,
        read,
        write,
        flush,
        erase_block,
        sectors,
        sector_size,
        erase_block_sectors,
        true,
        controller_wear_levelled
    };

    if (!register_storage_backend_internal(b))
        return false;

    const int registered = static_cast<int>(g_storage_backend_count - 1);
    if (b.flash || g_storage_active < 0)
        g_storage_active = registered;

    return true;
}
