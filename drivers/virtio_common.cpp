#include "virtio_common.hpp"
#include "pci.hpp"
#include "dma.hpp"

extern "C" {
#include <efi.h>
}

extern "C" {
#include <efilib.h>
}

#include <stdint.h>

// ============================================================
// Minimal freestanding memset
// ============================================================

static void* blockos_memset(
    void* ptr,
    int value,
    uint64_t size
)
{
    uint8_t* dst =
        static_cast<uint8_t*>(ptr);

    const uint8_t byte =
        static_cast<uint8_t>(value);

    for (uint64_t i = 0; i < size; ++i)
        dst[i] = byte;

    return ptr;
}

// ============================================================
// VirtIO legacy PCI I/O helpers
// ============================================================

static inline void outb_io(uint16_t port, uint8_t value)
{
    __asm__ volatile (
        "outb %0, %1"
        :
        : "a"(value), "dN"(port)
    );
}

static inline uint8_t inb_io(uint16_t port)
{
    uint8_t value;

    __asm__ volatile (
        "inb %1, %0"
        : "=a"(value)
        : "dN"(port)
    );

    return value;
}

static inline void outw_io(uint16_t port, uint16_t value)
{
    __asm__ volatile (
        "outw %0, %1"
        :
        : "a"(value), "dN"(port)
    );
}

static inline uint16_t inw_io(uint16_t port)
{
    uint16_t value;

    __asm__ volatile (
        "inw %1, %0"
        : "=a"(value)
        : "dN"(port)
    );

    return value;
}

static inline void outl_io(uint16_t port, uint32_t value)
{
    __asm__ volatile (
        "outl %0, %1"
        :
        : "a"(value), "dN"(port)
    );
}

static inline uint32_t inl_io(uint16_t port)
{
    uint32_t value;

    __asm__ volatile (
        "inl %1, %0"
        : "=a"(value)
        : "dN"(port)
    );

    return value;
}

// ============================================================
// MMIO helpers
// ============================================================

static uint8_t read_reg8_mmio(
    uint64_t base,
    uint32_t offset
)
{
    volatile uint8_t* p =
        (volatile uint8_t*)(UINTN)(base + offset);

    return *p;
}

static void write_reg8_mmio(
    uint64_t base,
    uint32_t offset,
    uint8_t value
)
{
    volatile uint8_t* p =
        (volatile uint8_t*)(UINTN)(base + offset);

    *p = value;
}

static uint16_t read_reg16_mmio(
    uint64_t base,
    uint32_t offset
)
{
    volatile uint16_t* p =
        (volatile uint16_t*)(UINTN)(base + offset);

    return *p;
}

static void write_reg16_mmio(
    uint64_t base,
    uint32_t offset,
    uint16_t value
)
{
    volatile uint16_t* p =
        (volatile uint16_t*)(UINTN)(base + offset);

    *p = value;
}

static uint32_t read_reg32_mmio(
    uint64_t base,
    uint32_t offset
)
{
    volatile uint32_t* p =
        (volatile uint32_t*)(UINTN)(base + offset);

    return *p;
}

static void write_reg32_mmio(
    uint64_t base,
    uint32_t offset,
    uint32_t value
)
{
    volatile uint32_t* p =
        (volatile uint32_t*)(UINTN)(base + offset);

    *p = value;
}

// ============================================================
// Generic register access
// ============================================================

static uint8_t virtio_read8(
    const virtio_common::DeviceHandle* h,
    uint32_t offset
)
{
    if (!h)
        return 0;

    if (h->mmio)
        return read_reg8_mmio(h->bar0, offset);

    return inb_io(
        (uint16_t)(h->bar0 + offset)
    );
}

static void virtio_write8(
    const virtio_common::DeviceHandle* h,
    uint32_t offset,
    uint8_t value
)
{
    if (!h)
        return;

    if (h->mmio)
    {
        write_reg8_mmio(
            h->bar0,
            offset,
            value
        );

        return;
    }

    outb_io(
        (uint16_t)(h->bar0 + offset),
        value
    );
}

static uint16_t virtio_read16(
    const virtio_common::DeviceHandle* h,
    uint32_t offset
)
{
    if (!h)
        return 0;

    if (h->mmio)
        return read_reg16_mmio(h->bar0, offset);

    return inw_io(
        (uint16_t)(h->bar0 + offset)
    );
}

static void virtio_write16(
    const virtio_common::DeviceHandle* h,
    uint32_t offset,
    uint16_t value
)
{
    if (!h)
        return;

    if (h->mmio)
    {
        write_reg16_mmio(
            h->bar0,
            offset,
            value
        );

        return;
    }

    outw_io(
        (uint16_t)(h->bar0 + offset),
        value
    );
}

static uint32_t virtio_read32(
    const virtio_common::DeviceHandle* h,
    uint32_t offset
)
{
    if (!h)
        return 0;

    if (h->mmio)
        return read_reg32_mmio(
            h->bar0,
            offset
        );

    return inl_io(
        (uint16_t)(h->bar0 + offset)
    );
}

static void virtio_write32(
    const virtio_common::DeviceHandle* h,
    uint32_t offset,
    uint32_t value
)
{
    if (!h)
        return;

    if (h->mmio)
    {
        write_reg32_mmio(
            h->bar0,
            offset,
            value
        );

        return;
    }

    outl_io(
        (uint16_t)(h->bar0 + offset),
        value
    );
}

// ============================================================
// Legacy VirtIO PCI registers
// ============================================================

namespace
{
    constexpr uint32_t REG_HOST_FEATURES   = 0x00;
    constexpr uint32_t REG_GUEST_FEATURES  = 0x04;
    constexpr uint32_t REG_GUEST_PAGE_SIZE = 0x08;
    constexpr uint32_t REG_QUEUE_SELECT    = 0x0C;
    constexpr uint32_t REG_QUEUE_SIZE      = 0x0E;
    constexpr uint32_t REG_QUEUE_PFN       = 0x10;
    constexpr uint32_t REG_STATUS          = 0x12;
    constexpr uint32_t REG_ISR             = 0x13;
    constexpr uint32_t REG_DEVICE_CONFIG   = 0x14;

    constexpr uint8_t STATUS_ACKNOWLEDGE = 0x01;
    constexpr uint8_t STATUS_DRIVER      = 0x02;
    constexpr uint8_t STATUS_DRIVER_OK   = 0x04;
    constexpr uint8_t STATUS_FEATURES_OK = 0x08;
    constexpr uint8_t STATUS_FAILED      = 0x80;

    constexpr uint32_t VIRTIO_NET_F_MAC =
        (1u << 5);

    constexpr uint16_t VIRTIO_VENDOR_ID =
        0x1AF4;

    constexpr uint16_t VIRTIO_NET_LEGACY_DEVICE_ID =
        0x1000;

    constexpr uint16_t VIRTIO_NET_MODERN_DEVICE_ID =
        0x1041;
}

// ============================================================
// VirtIO device discovery
// ============================================================

bool virtio_common::probe_device(
    virtio_common::DeviceType type,
    virtio_common::DeviceHandle* h
)
{
    if (!h)
        return false;

    blockos_memset(
        h,
        0,
        sizeof(*h)
    );

    uint16_t wanted_device = 0;

    switch (type)
    {
        case DeviceType::NETWORK:
            wanted_device =
                VIRTIO_NET_LEGACY_DEVICE_ID;
            break;

        default:
            return false;
    }

    for (uint32_t bus = 0; bus < 256; ++bus)
    {
        for (uint32_t slot = 0; slot < 32; ++slot)
        {
            for (uint32_t func = 0; func < 8; ++func)
            {
                if (!pci_device_exists(
                        (uint8_t)bus,
                        (uint8_t)slot,
                        (uint8_t)func))
                {
                    continue;
                }

                const uint16_t vendor =
                    pci_cfg_read16(
                        (uint8_t)bus,
                        (uint8_t)slot,
                        (uint8_t)func,
                        0x00
                    );

                const uint16_t device =
                    pci_cfg_read16(
                        (uint8_t)bus,
                        (uint8_t)slot,
                        (uint8_t)func,
                        0x02
                    );

                if (vendor != VIRTIO_VENDOR_ID)
                    continue;

                if (device != wanted_device)
                    continue;

                const uint64_t bar =
                    pci_read_bar(
                        (uint8_t)bus,
                        (uint8_t)slot,
                        (uint8_t)func,
                        0
                    );

                if (bar == 0)
                    continue;

                h->device_id = device;
                h->bus = (uint8_t)bus;
                h->slot = (uint8_t)slot;
                h->func = (uint8_t)func;
                h->bar0 = bar;
                h->mmio = false;
                h->vendor_id = vendor;

                h->irq =
                    pci_cfg_read8(
                        (uint8_t)bus,
                        (uint8_t)slot,
                        (uint8_t)func,
                        0x3C
                    );

                CHAR16 msg[256];

                UnicodeSPrint(
                    msg,
                    sizeof(msg),
                    (CHAR16*)
                    L"virtio_common: VirtIO network found "
                    L"at %u:%u.%u BAR0=0x%lx\n",
                    bus,
                    slot,
                    func,
                    bar
                );

                Print(msg);

                return true;
            }
        }
    }

    Print(
        (CHAR16*)
        L"virtio_common: VirtIO network device not found\n"
    );

    return false;
}

// ============================================================
// Reset device
// ============================================================

static bool virtio_reset(
    virtio_common::DeviceHandle* h
)
{
    if (!h)
        return false;

    virtio_write8(
        h,
        REG_STATUS,
        0
    );

    for (volatile uint32_t i = 0;
         i < 10000;
         ++i)
    {
        if (virtio_read8(
                h,
                REG_STATUS) == 0)
        {
            return true;
        }
    }

    return virtio_read8(
        h,
        REG_STATUS
    ) == 0;
}

// ============================================================
// Feature negotiation
// ============================================================

static bool negotiate_legacy_network_features(
    virtio_common::DeviceHandle* h
)
{
    if (!h)
        return false;

    const uint32_t host_features =
        virtio_read32(
            h,
            REG_HOST_FEATURES
        );

    const uint32_t wanted_features =
        VIRTIO_NET_F_MAC;

    const uint32_t agreed_features =
        host_features &
        wanted_features;

    if ((agreed_features &
         VIRTIO_NET_F_MAC) == 0)
    {
        Print(
            (CHAR16*)
            L"virtio_common: VirtIO-net MAC feature unavailable\n"
        );

        return false;
    }

    virtio_write32(
        h,
        REG_GUEST_FEATURES,
        agreed_features
    );

    CHAR16 msg[256];

    UnicodeSPrint(
        msg,
        sizeof(msg),
        (CHAR16*)
        L"virtio_common: host_features=0x%08x\n"
        L"virtio_common: wanted_features=0x%08x\n"
        L"virtio_common: agreed_features=0x%08x\n",
        host_features,
        wanted_features,
        agreed_features
    );

    Print(msg);

    return true;
}

// ============================================================
// Legacy VirtIO device initialization
// ============================================================

bool virtio_common::device_init(
    virtio_common::DeviceHandle* h
)
{
    if (!h)
        return false;

    CHAR16 msg[256];

    UnicodeSPrint(
        msg,
        sizeof(msg),
        (CHAR16*)
        L"virtio_common: initializing "
        L"device=0x%04x at %u:%u.%u BAR0=0x%lx\n",
        h->device_id,
        h->bus,
        h->slot,
        h->func,
        h->bar0
    );

    Print(msg);

    Print(
        (CHAR16*)
        L"virtio_common: resetting device\n"
    );

    if (!virtio_reset(h))
    {
        Print(
            (CHAR16*)
            L"virtio_common: device reset failed\n"
        );

        return false;
    }

    Print(
        (CHAR16*)
        L"virtio_common: device reset OK\n"
    );

    virtio_write8(
        h,
        REG_STATUS,
        STATUS_ACKNOWLEDGE
    );

    virtio_write8(
        h,
        REG_STATUS,
        STATUS_ACKNOWLEDGE |
        STATUS_DRIVER
    );

    uint8_t status =
        virtio_read8(
            h,
            REG_STATUS
        );

    UnicodeSPrint(
        msg,
        sizeof(msg),
        (CHAR16*)
        L"virtio_common: status after DRIVER=0x%02x\n",
        status
    );

    Print(msg);

    if ((status &
         STATUS_DRIVER) == 0)
    {
        Print(
            (CHAR16*)
            L"virtio_common: DRIVER status not accepted\n"
        );

        return false;
    }

    Print(
        (CHAR16*)
        L"virtio_common: negotiating features\n"
    );

    if (h->device_id ==
        VIRTIO_NET_LEGACY_DEVICE_ID)
    {
        if (!negotiate_legacy_network_features(h))
        {
            Print(
                (CHAR16*)
                L"virtio_common: feature negotiation failed\n"
            );

            virtio_write8(
                h,
                REG_STATUS,
                STATUS_ACKNOWLEDGE |
                STATUS_DRIVER |
                STATUS_FAILED
            );

            return false;
        }
    }
    else
    {
        Print(
            (CHAR16*)
            L"virtio_common: unsupported VirtIO device mode\n"
        );

        return false;
    }

    virtio_write32(
        h,
        REG_GUEST_PAGE_SIZE,
        4096
    );

    const uint32_t page_size =
        virtio_read32(
            h,
            REG_GUEST_PAGE_SIZE
        );

    UnicodeSPrint(
        msg,
        sizeof(msg),
        (CHAR16*)
        L"virtio_common: guest_page_size=%u\n",
        page_size
    );

    Print(msg);

    if (page_size != 4096)
    {
        Print(
            (CHAR16*)
            L"virtio_common: invalid guest page size\n"
        );

        return false;
    }

    status =
        virtio_read8(
            h,
            REG_STATUS
        );

    status |=
        STATUS_FEATURES_OK;

    virtio_write8(
        h,
        REG_STATUS,
        status
    );

    status =
        virtio_read8(
            h,
            REG_STATUS
        );

    UnicodeSPrint(
        msg,
        sizeof(msg),
        (CHAR16*)
        L"virtio_common: status after FEATURES_OK=0x%02x\n",
        status
    );

    Print(msg);

    if ((status &
         STATUS_FEATURES_OK) == 0)
    {
        Print(
            (CHAR16*)
            L"virtio_common: device rejected FEATURES_OK\n"
        );

        virtio_write8(
            h,
            REG_STATUS,
            status |
            STATUS_FAILED
        );

        return false;
    }

    void* test_dma =
        dma::alloc(
            4096,
            4096
        );

    if (!test_dma)
    {
        Print(
            (CHAR16*)
            L"virtio_common: DMA allocation failed\n"
        );

        virtio_write8(
            h,
            REG_STATUS,
            status |
            STATUS_FAILED
        );

        return false;
    }

    UnicodeSPrint(
        msg,
        sizeof(msg),
        (CHAR16*)
        L"virtio_common: DMA test OK at %p\n",
        test_dma
    );

    Print(msg);

    Print(
        (CHAR16*)
        L"virtio_common: reset + feature negotiation complete\n"
    );

    return true;
}

// ============================================================
// Legacy host feature read
// ============================================================

uint32_t virtio_common::read_host_features(
    void* bar0,
    bool mmio
)
{
    if (!bar0)
        return 0;

    if (mmio)
    {
        return read_reg32_mmio(
            (uint64_t)(UINTN)bar0,
            REG_HOST_FEATURES
        );
    }

    return inl_io(
        (uint16_t)(
            (uint64_t)(UINTN)bar0 +
            REG_HOST_FEATURES
        )
    );
}

// ============================================================
// Legacy feature negotiation helper
// ============================================================

bool virtio_common::negotiate_features(
    void* bar0,
    bool mmio,
    uint32_t want_mask
)
{
    if (!bar0)
        return false;

    const uint32_t host =
        virtio_common::read_host_features(
            bar0,
            mmio
        );

    const uint32_t agreed =
        host &
        want_mask;

    if (mmio)
    {
        write_reg32_mmio(
            (uint64_t)(UINTN)bar0,
            REG_GUEST_FEATURES,
            agreed
        );
    }
    else
    {
        outl_io(
            (uint16_t)(
                (uint64_t)(UINTN)bar0 +
                REG_GUEST_FEATURES
            ),
            agreed
        );
    }

    CHAR16 msg[256];

    UnicodeSPrint(
        msg,
        sizeof(msg),
        (CHAR16*)
        L"virtio_common: host_features=0x%08x "
        L"want=0x%08x agreed=0x%08x\n",
        host,
        want_mask,
        agreed
    );

    Print(msg);

    return true;
}
