#include <efi.h>
#include <efilib.h>

#include "device_manager.hpp"
#include "../fs/vfs.hpp"
#include "gui.hpp"

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


/* --------------------------------------------------------------------------
 * BlockOS embedded/space safety services.
 * Kept in this existing driver manager so the normal kernel build needs no
 * additional HAL/source directory.
 * -------------------------------------------------------------------------- */
#include "io.hpp"
#include "../net/udp.hpp"

namespace {

EFI_RUNTIME_SERVICES* g_runtime = nullptr;
bool g_secure_boot_known = false;
bool g_secure_boot = false;
bool g_recovery_requested = false;
bool g_telecommand_bound = false;
uint32_t g_watchdog_heartbeat = 0;
uint32_t g_watchdog_resets = 0;
uint64_t g_last_heartbeat_ms = 0;
constexpr uint64_t g_watchdog_timeout_ms = 10000;
constexpr uint16_t SPACE_CMD_PORT = 4242;

struct SpaceState {
    uint64_t generation;
    uint32_t watchdog_resets;
    uint32_t heartbeat;
};
SpaceState g_state_a{1, 0, 0};
SpaceState g_state_b{0, 0, 0};

struct MmioBus {
    uint64_t base;
    uint32_t stride;
};
MmioBus g_gpio{0, 0};
MmioBus g_i2c{0, 0};
MmioBus g_spi{0, 0};
MmioBus g_can{0, 0};
MmioBus g_adc{0, 0};
MmioBus g_dac{0, 0};

static uint8_t parity64(uint64_t v)
{
    uint8_t p = 0;
    while (v) {
        p ^= static_cast<uint8_t>(v & 1u);
        v >>= 1;
    }
    return p;
}

static uint8_t ecc_encode64(uint64_t value)
{
    uint8_t p = 0;
    p |= static_cast<uint8_t>(parity64(value) & 1u);
    p |= static_cast<uint8_t>((parity64(value & 0x5555555555555555ULL) & 1u) << 1);
    p |= static_cast<uint8_t>((parity64(value & 0x3333333333333333ULL) & 1u) << 2);
    p |= static_cast<uint8_t>((parity64(value & 0x0F0F0F0F0F0F0F0FULL) & 1u) << 3);
    p |= static_cast<uint8_t>((parity64(value & 0x00FF00FF00FF00FFULL) & 1u) << 4);
    p |= static_cast<uint8_t>((parity64(value & 0x0000FFFF0000FFFFULL) & 1u) << 5);
    p |= static_cast<uint8_t>((parity64(value & 0x00000000FFFFFFFFULL) & 1u) << 6);
    return p;
}

static bool ecc_check64(uint64_t* value, uint8_t stored, bool* corrected)
{
    if (corrected) *corrected = false;
    if (!value) return false;
    uint8_t now = ecc_encode64(*value);
    if (now == stored) return true;
    /* This compact ECC record detects corruption. It deliberately does not
       guess a bit location when the compact parity syndromes are ambiguous. */
    return false;
}

static uint32_t crc32(const void* data, size_t len)
{
    const uint8_t* p = static_cast<const uint8_t*>(data);
    uint32_t crc = 0xFFFFFFFFu;
    if (!p && len) return 0;
    for (size_t i = 0; i < len; ++i) {
        crc ^= p[i];
        for (unsigned b = 0; b < 8; ++b)
            crc = (crc >> 1) ^ (0xEDB88320u & (-(int32_t)(crc & 1u)));
    }
    return ~crc;
}

static void uart_init() {}
static void uart_puts(const char*) {}

static bool read_secure_boot(EFI_SYSTEM_TABLE*)
{
    g_secure_boot_known = false;
    g_secure_boot = false;
    return false;
}

static bool read_recovery_request(EFI_SYSTEM_TABLE*)
{
    return false;
}

static void state_commit(uint32_t flags)
{
    g_state_b = g_state_a;
    ++g_state_a.generation;
    g_state_a.watchdog_resets = g_watchdog_resets;
    g_state_a.heartbeat = g_watchdog_heartbeat;
    (void)flags;
}

static void runtime_reset()
{
    ++g_watchdog_resets;
    if (g_runtime && g_runtime->ResetSystem)
        g_runtime->ResetSystem(EfiResetCold, EFI_SUCCESS, 0, nullptr);
}

static void idle_once()
{
    __asm__ volatile("pause");
}

struct RtcTime {
    uint16_t y;
    uint8_t mo, d, h, m, s;
};

static uint8_t cmos_read(uint8_t reg)
{
    io::outb(0x70, reg);
    return io::inb(0x71);
}

static void rtc_read(uint16_t* y, uint8_t* mo, uint8_t* d,
                     uint8_t* h, uint8_t* m, uint8_t* s)
{
    if (!y || !mo || !d || !h || !m || !s) return;
    *s = cmos_read(0x00);
    *m = cmos_read(0x02);
    *h = cmos_read(0x04);
    *d = cmos_read(0x07);
    *mo = cmos_read(0x08);
    *y = static_cast<uint16_t>(2000u + cmos_read(0x09));
}

static bool gpio_write(unsigned bit, bool high)
{
    if (!g_gpio.base) return false;
    volatile uint32_t* r = reinterpret_cast<volatile uint32_t*>(g_gpio.base + bit * g_gpio.stride);
    *r = high ? 1u : 0u;
    return true;
}

static bool gpio_read(unsigned bit, bool* high)
{
    if (!g_gpio.base || !high) return false;
    volatile const uint32_t* r = reinterpret_cast<volatile const uint32_t*>(g_gpio.base + bit * g_gpio.stride);
    *high = ((*r) & 1u) != 0;
    return true;
}

static bool mmio_transfer(const MmioBus& bus, const uint8_t* tx, size_t tx_len,
                          uint8_t* rx, size_t rx_len)
{
    if (!bus.base) return false;
    volatile uint8_t* r = reinterpret_cast<volatile uint8_t*>(bus.base);
    for (size_t i = 0; i < tx_len; ++i) r[i * (bus.stride ? bus.stride : 1u)] = tx ? tx[i] : 0;
    for (size_t i = 0; i < rx_len; ++i) if (rx) rx[i] = r[i * (bus.stride ? bus.stride : 1u)];
    return true;
}

static bool i2c_transfer(uint8_t, const uint8_t* tx, size_t tx_len,
                         uint8_t* rx, size_t rx_len)
{
    return mmio_transfer(g_i2c, tx, tx_len, rx, rx_len);
}

static bool spi_transfer(const uint8_t* tx, uint8_t* rx, size_t len)
{
    return mmio_transfer(g_spi, tx, len, rx, len);
}

static bool can_send(uint32_t id, const uint8_t* data, uint8_t len)
{
    if (!g_can.base || len > 8) return false;
    volatile uint32_t* r = reinterpret_cast<volatile uint32_t*>(g_can.base);
    r[0] = id;
    r[1] = len;
    const uint32_t* p = reinterpret_cast<const uint32_t*>(data);
    for (unsigned i = 0; i < 2; ++i) r[2 + i] = (p && i * 4 < len) ? p[i] : 0;
    return true;
}

static bool can_recv(uint32_t* id, uint8_t* data, uint8_t* len)
{
    if (!g_can.base || !id || !data || !len) return false;
    volatile const uint32_t* r = reinterpret_cast<volatile const uint32_t*>(g_can.base);
    *id = r[0];
    *len = static_cast<uint8_t>(r[1] > 8 ? 8 : r[1]);
    for (unsigned i = 0; i < 2; ++i) {
        const uint32_t word = r[2 + i];
        const size_t off = static_cast<size_t>(i) * 4u;
        if (off < *len) data[off] = static_cast<uint8_t>(word & 0xFFu);
        if (off + 1u < *len) data[off + 1u] = static_cast<uint8_t>((word >> 8) & 0xFFu);
        if (off + 2u < *len) data[off + 2u] = static_cast<uint8_t>((word >> 16) & 0xFFu);
        if (off + 3u < *len) data[off + 3u] = static_cast<uint8_t>((word >> 24) & 0xFFu);
    }
    return *len != 0;
}

static bool adc_read(unsigned ch, uint16_t* value)
{
    if (!g_adc.base || !value) return false;
    volatile uint16_t* r = reinterpret_cast<volatile uint16_t*>(g_adc.base + ch * (g_adc.stride ? g_adc.stride : 2u));
    *value = *r;
    return true;
}

static bool dac_write(unsigned ch, uint16_t value)
{
    if (!g_dac.base) return false;
    volatile uint16_t* r = reinterpret_cast<volatile uint16_t*>(g_dac.base + ch * (g_dac.stride ? g_dac.stride : 2u));
    *r = value;
    return true;
}

static void command_callback(const blockos::net::UdpDatagram& d)
{
    if (d.length == 0 || !d.data) return;
    switch (d.data[0]) {
        case 0x01: g_watchdog_heartbeat = 1; break;
        case 0x02: state_commit(0x04u); break;
        case 0x03: g_recovery_requested = true; break;
        default: break;
    }
}

} // namespace

extern "C" bool blockos_space_init(EFI_SYSTEM_TABLE* st)
{
    if (!st) return false;
    g_runtime = st->RuntimeServices;
    read_secure_boot(st);
    g_recovery_requested = read_recovery_request(st);
    uart_init();
    uart_puts("[BlockOS] space/embedded safety services online\r\n");
    g_watchdog_heartbeat = 1;
    g_last_heartbeat_ms = 0;
    state_commit(g_secure_boot ? 0x1u : (g_recovery_requested ? 0x4u : 0u));
    if (!g_telecommand_bound) {
        g_telecommand_bound = blockos::net::udp_bind(SPACE_CMD_PORT, command_callback);
    }
    return true;
}

extern "C" void blockos_space_tick(uint64_t now_ms)
{
    ++g_watchdog_heartbeat;
    if (g_last_heartbeat_ms == 0) g_last_heartbeat_ms = now_ms;
    if (now_ms >= g_last_heartbeat_ms && now_ms - g_last_heartbeat_ms > g_watchdog_timeout_ms) {
        state_commit(0x2u);
        runtime_reset();
        return;
    }
    uint64_t probe_a = (g_state_a.generation << 32) ^ g_state_a.watchdog_resets ^ g_state_a.heartbeat;
    uint8_t ecc_a = ecc_encode64(probe_a);
    bool corrected = false;
    (void)ecc_check64(&probe_a, ecc_a, &corrected);
    state_commit(g_secure_boot ? 0x1u : 0u);
    RtcTime now{};
    rtc_read(&now.y, &now.mo, &now.d, &now.h, &now.m, &now.s);
    (void)now;
    g_last_heartbeat_ms = now_ms;
}

extern "C" bool blockos_space_recovery_requested() { return g_recovery_requested; }
extern "C" bool blockos_space_secure_boot() { return g_secure_boot_known && g_secure_boot; }
extern "C" void blockos_space_idle() { idle_once(); }
extern "C" bool blockos_space_ecc64(uint64_t* value, uint8_t ecc, bool* corrected) { return ecc_check64(value, ecc, corrected); }
extern "C" uint8_t blockos_space_ecc64_encode(uint64_t value) { return ecc_encode64(value); }
extern "C" uint32_t blockos_space_crc32(const void* data, size_t len) { return crc32(data, len); }
extern "C" bool blockos_space_gpio_write(unsigned bit, bool high) { return gpio_write(bit, high); }
extern "C" bool blockos_space_gpio_read(unsigned bit, bool* high) { return gpio_read(bit, high); }
extern "C" bool blockos_space_i2c(uint8_t addr, const uint8_t* tx, size_t tx_len, uint8_t* rx, size_t rx_len) { (void)addr; return i2c_transfer(addr, tx, tx_len, rx, rx_len); }
extern "C" bool blockos_space_spi(const uint8_t* tx, uint8_t* rx, size_t len) { return spi_transfer(tx, rx, len); }
extern "C" bool blockos_space_can_send(uint32_t id, const uint8_t* data, uint8_t len) { return can_send(id, data, len); }
extern "C" bool blockos_space_can_recv(uint32_t* id, uint8_t* data, uint8_t* len) { return can_recv(id, data, len); }
extern "C" bool blockos_space_adc(unsigned ch, uint16_t* value) { return adc_read(ch, value); }
extern "C" bool blockos_space_dac(unsigned ch, uint16_t value) { return dac_write(ch, value); }
extern "C" void blockos_space_set_gpio_base(uint64_t base, uint32_t stride) { g_gpio = {base, stride}; }
extern "C" void blockos_space_set_i2c_base(uint64_t base, uint32_t stride) { g_i2c = {base, stride}; }
extern "C" void blockos_space_set_spi_base(uint64_t base, uint32_t stride) { g_spi = {base, stride}; }
extern "C" void blockos_space_set_can_base(uint64_t base, uint32_t stride) { g_can = {base, stride}; }
extern "C" void blockos_space_set_adc_base(uint64_t base, uint32_t stride) { g_adc = {base, stride}; }
extern "C" void blockos_space_set_dac_base(uint64_t base, uint32_t stride) { g_dac = {base, stride}; }
