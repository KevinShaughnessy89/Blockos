#include "virtio_net_driver.hpp"
#include "virtio_net_internal.hpp"

#include "virtio_common.hpp"
#include "virtqueue.hpp"
#include "virtqueue_ops.hpp"
#include "dma.hpp"
#include "virtio_service.hpp"

#include "../drivers/pci.hpp"

extern "C" {
#include <efi.h>
}

extern "C" {
#include <efilib.h>
}

#include <stdint.h>
#include <stddef.h>
#include <string.h>

namespace {

constexpr uint32_t RX_QUEUE = 0;
constexpr uint32_t TX_QUEUE = 1;

constexpr uint32_t RX_QUEUE_SIZE = 128;
constexpr uint32_t TX_QUEUE_SIZE = 128;

constexpr uint32_t RX_BUFFER_SIZE = 2048;

constexpr uint32_t VIRTIO_NET_HDR_SIZE = 10;

constexpr uint16_t REG_HOST_FEATURES   = 0x00;
constexpr uint16_t REG_GUEST_FEATURES  = 0x04;
constexpr uint16_t REG_GUEST_PAGE_SIZE = 0x08;
constexpr uint16_t REG_QUEUE_SELECT    = 0x0C;
constexpr uint16_t REG_QUEUE_NUM       = 0x0E;
constexpr uint16_t REG_QUEUE_PFN       = 0x10;
constexpr uint16_t REG_QUEUE_NOTIFY    = 0x10;
constexpr uint16_t REG_STATUS          = 0x12;

constexpr uint16_t NET_CONFIG_MAC = 0x14;

constexpr uint8_t STATUS_ACKNOWLEDGE = 1;
constexpr uint8_t STATUS_DRIVER      = 2;
constexpr uint8_t STATUS_DRIVER_OK   = 4;
constexpr uint8_t STATUS_FEATURES_OK = 8;
constexpr uint8_t STATUS_FAILED      = 128;

constexpr uint32_t VIRTIO_NET_F_MAC = 1u << 5;

constexpr uint16_t VRING_DESC_F_WRITE = 2;

constexpr size_t QUEUE_MEMORY_SIZE = 16384;

static virtio_common::DeviceHandle g_device{};

static void* g_rx_queue_mem = nullptr;
static void* g_tx_queue_mem = nullptr;

static void* g_rx_buffers[RX_QUEUE_SIZE]{};

static bool g_initialized = false;
static bool g_queues_ready = false;

static uint8_t g_mac[6]{};


// ------------------------------------------------------------
// PCI I/O
// ------------------------------------------------------------

static inline void io_out8(
    uint16_t port,
    uint8_t value
)
{
    __asm__ volatile(
        "outb %0, %1"
        :
        : "a"(value), "dN"(port)
    );
}


static inline uint8_t io_in8(
    uint16_t port
)
{
    uint8_t value;

    __asm__ volatile(
        "inb %1, %0"
        : "=a"(value)
        : "dN"(port)
    );

    return value;
}


static inline void io_out16(
    uint16_t port,
    uint16_t value
)
{
    __asm__ volatile(
        "outw %0, %1"
        :
        : "a"(value), "dN"(port)
    );
}


static inline uint16_t io_in16(
    uint16_t port
)
{
    uint16_t value;

    __asm__ volatile(
        "inw %1, %0"
        : "=a"(value)
        : "dN"(port)
    );

    return value;
}


static inline void io_out32(
    uint16_t port,
    uint32_t value
)
{
    __asm__ volatile(
        "outl %0, %1"
        :
        : "a"(value), "dN"(port)
    );
}


static inline uint32_t io_in32(
    uint16_t port
)
{
    uint32_t value;

    __asm__ volatile(
        "inl %1, %0"
        : "=a"(value)
        : "dN"(port)
    );

    return value;
}


// ------------------------------------------------------------
// Legacy VirtIO register access
// ------------------------------------------------------------

static uint8_t read_status()
{
    if (g_device.mmio)
    {
        volatile uint8_t* p =
            reinterpret_cast<volatile uint8_t*>(
                static_cast<UINTN>(
                    g_device.bar0 + REG_STATUS
                )
            );

        return *p;
    }

    return io_in8(
        static_cast<uint16_t>(
            g_device.bar0 + REG_STATUS
        )
    );
}


static void write_status(
    uint8_t status
)
{
    if (g_device.mmio)
    {
        volatile uint8_t* p =
            reinterpret_cast<volatile uint8_t*>(
                static_cast<UINTN>(
                    g_device.bar0 + REG_STATUS
                )
            );

        *p = status;
        return;
    }

    io_out8(
        static_cast<uint16_t>(
            g_device.bar0 + REG_STATUS
        ),
        status
    );
}


static uint32_t read_reg32(
    uint16_t offset
)
{
    if (g_device.mmio)
    {
        volatile uint32_t* p =
            reinterpret_cast<volatile uint32_t*>(
                static_cast<UINTN>(
                    g_device.bar0 + offset
                )
            );

        return *p;
    }

    return io_in32(
        static_cast<uint16_t>(
            g_device.bar0 + offset
        )
    );
}


static void write_reg32(
    uint16_t offset,
    uint32_t value
)
{
    if (g_device.mmio)
    {
        volatile uint32_t* p =
            reinterpret_cast<volatile uint32_t*>(
                static_cast<UINTN>(
                    g_device.bar0 + offset
                )
            );

        *p = value;
        return;
    }

    io_out32(
        static_cast<uint16_t>(
            g_device.bar0 + offset
        ),
        value
    );
}


static uint16_t read_reg16(
    uint16_t offset
)
{
    if (g_device.mmio)
    {
        volatile uint16_t* p =
            reinterpret_cast<volatile uint16_t*>(
                static_cast<UINTN>(
                    g_device.bar0 + offset
                )
            );

        return *p;
    }

    return io_in16(
        static_cast<uint16_t>(
            g_device.bar0 + offset
        )
    );
}


static void write_reg16(
    uint16_t offset,
    uint16_t value
)
{
    if (g_device.mmio)
    {
        volatile uint16_t* p =
            reinterpret_cast<volatile uint16_t*>(
                static_cast<UINTN>(
                    g_device.bar0 + offset
                )
            );

        *p = value;
        return;
    }

    io_out16(
        static_cast<uint16_t>(
            g_device.bar0 + offset
        ),
        value
    );
}


// ------------------------------------------------------------
// Queue notification
// ------------------------------------------------------------

static void notify_queue(
    uint16_t queue_index
)
{
    if (g_device.mmio)
    {
        volatile uint16_t* p =
            reinterpret_cast<volatile uint16_t*>(
                static_cast<UINTN>(
                    g_device.bar0 + REG_QUEUE_NOTIFY
                )
            );

        *p = queue_index;
        return;
    }

    io_out16(
        static_cast<uint16_t>(
            g_device.bar0 + REG_QUEUE_NOTIFY
        ),
        queue_index
    );
}


// ------------------------------------------------------------
// Queue setup
// ------------------------------------------------------------

static bool setup_legacy_queue(
    uint16_t queue_index,
    VirtQueueView* view,
    void* memory,
    uint32_t requested_size
)
{
    if (view == nullptr)
        return false;

    if (memory == nullptr)
        return false;

    write_reg16(
        REG_QUEUE_SELECT,
        queue_index
    );

    const uint16_t max_size =
        read_reg16(
            REG_QUEUE_NUM
        );

    if (max_size == 0)
        return false;

    uint32_t actual_size =
        requested_size;

    if (actual_size > max_size)
        actual_size = max_size;

    if (actual_size != requested_size)
        return false;

    const uintptr_t address =
        reinterpret_cast<uintptr_t>(
            memory
        );

    if ((address & 4095u) != 0)
        return false;

    *view =
        virtqueue_ops::view_from_mem(
            memory,
            actual_size
        );

    if (view->desc == nullptr ||
        view->avail == nullptr ||
        view->used == nullptr)
    {
        return false;
    }

    virtqueue_ops::init_rings(
        view
    );

    const uint32_t pfn =
        static_cast<uint32_t>(
            address >> 12
        );

    write_reg32(
        REG_QUEUE_PFN,
        pfn
    );

    const uint32_t readback =
        read_reg32(
            REG_QUEUE_PFN
        );

    if (readback != pfn)
        return false;

    return true;
}


// ------------------------------------------------------------
// RX buffer
// ------------------------------------------------------------

static bool prepare_rx_descriptor(
    uint32_t index
)
{
    if (index >= RX_QUEUE_SIZE)
        return false;

    void* buffer =
        dma::alloc(
            RX_BUFFER_SIZE,
            4096
        );

    if (buffer == nullptr)
        return false;

    memset(
        buffer,
        0,
        RX_BUFFER_SIZE
    );

    g_rx_buffers[index] =
        buffer;

    const uint64_t address =
        static_cast<uint64_t>(
            reinterpret_cast<UINTN>(
                buffer
            )
        );

    virtqueue_ops::set_descriptor(
        &virtio_net::g_rx_vq,
        index,
        address,
        RX_BUFFER_SIZE,
        VRING_DESC_F_WRITE,
        0
    );

    virtqueue_ops::submit_descriptor(
        &virtio_net::g_rx_vq,
        index
    );

    return true;
}


// ------------------------------------------------------------
// RX queue initialization
// ------------------------------------------------------------

static bool initialize_rx_queue()
{
    for (uint32_t i = 0;
         i < RX_QUEUE_SIZE;
         ++i)
    {
        if (!prepare_rx_descriptor(i))
            return false;
    }

    notify_queue(
        RX_QUEUE
    );

    return true;
}


// ------------------------------------------------------------
// MAC
// ------------------------------------------------------------

static bool read_mac()
{
    volatile uint8_t* base =
        reinterpret_cast<volatile uint8_t*>(
            static_cast<UINTN>(
                g_device.bar0 +
                NET_CONFIG_MAC
            )
        );

    for (uint32_t i = 0;
         i < 6;
         ++i)
    {
        g_mac[i] =
            base[i];
    }

    bool all_zero = true;
    bool all_ff = true;

    for (int i = 0; i < 6; ++i)
    {
        if (g_mac[i] != 0)
            all_zero = false;

        if (g_mac[i] != 0xFF)
            all_ff = false;
    }

    if (all_zero || all_ff)
        return false;

    if (g_mac[0] & 1)
        return false;

    return true;
}


// ------------------------------------------------------------
// Feature negotiation
// ------------------------------------------------------------

static bool negotiate_features()
{
    const uint32_t host_features =
        read_reg32(
            REG_HOST_FEATURES
        );

    const uint32_t wanted =
        VIRTIO_NET_F_MAC;

    const uint32_t agreed =
        host_features & wanted;

    if ((agreed & VIRTIO_NET_F_MAC) == 0)
    {
        Print(
            (CHAR16*)
                L"virtio-net: device does not provide MAC feature\n"
        );

        return false;
    }

    write_reg32(
        REG_GUEST_FEATURES,
        agreed
    );

    return true;
}


// ------------------------------------------------------------
// Device initialization
// ------------------------------------------------------------

static bool initialize_device()
{
    write_status(0);

    if (read_status() != 0)
        return false;

    write_status(
        STATUS_ACKNOWLEDGE
    );

    write_status(
        STATUS_ACKNOWLEDGE |
        STATUS_DRIVER
    );

    write_reg32(
        REG_GUEST_PAGE_SIZE,
        4096
    );

    if (read_reg32(REG_GUEST_PAGE_SIZE) != 4096)
        return false;

    if (!negotiate_features())
        return false;

    write_status(
        STATUS_ACKNOWLEDGE |
        STATUS_DRIVER |
        STATUS_FEATURES_OK
    );

    const uint8_t status =
        read_status();

    if ((status & STATUS_FEATURES_OK) == 0)
    {
        write_status(
            STATUS_FAILED
        );

        return false;
    }

    return true;
}


// ------------------------------------------------------------
// Final DRIVER_OK
// ------------------------------------------------------------

static bool start_device()
{
    write_status(
        STATUS_ACKNOWLEDGE |
        STATUS_DRIVER |
        STATUS_FEATURES_OK |
        STATUS_DRIVER_OK
    );

    const uint8_t status =
        read_status();

    if (status & STATUS_FAILED)
        return false;

    if ((status & STATUS_DRIVER_OK) == 0)
        return false;

    return true;
}

} // anonymous namespace


namespace virtio_net {

bool g_ready = false;

VirtQueueView g_tx_vq{};
VirtQueueView g_rx_vq{};

TxSlot g_tx_slots[256]{};


// ------------------------------------------------------------
// TX slot helper
// ------------------------------------------------------------

static void clear_tx_slot(
    uint32_t index
)
{
    if (index >= 256)
        return;

    g_tx_slots[index].buf = nullptr;
    g_tx_slots[index].submit_tick = 0;
}


// ------------------------------------------------------------
// TX buffer pool
// ------------------------------------------------------------

void tx_pool_push(void* buf)
{
    /*
     * BlockOS DMA allocator currently does not expose dma::free().
     * The buffer therefore remains allocated.
     */
    (void)buf;
}


// ------------------------------------------------------------
// Initialization
// ------------------------------------------------------------

bool init()
{
    if (g_initialized)
        return g_ready;

    g_ready = false;
    g_initialized = false;
    g_queues_ready = false;

    memset(
        &g_device,
        0,
        sizeof(g_device)
    );

    memset(
        &g_tx_vq,
        0,
        sizeof(g_tx_vq)
    );

    memset(
        &g_rx_vq,
        0,
        sizeof(g_rx_vq)
    );

    memset(
        g_tx_slots,
        0,
        sizeof(g_tx_slots)
    );

    memset(
        g_rx_buffers,
        0,
        sizeof(g_rx_buffers)
    );

    memset(
        g_mac,
        0,
        sizeof(g_mac)
    );

    Print(
        (CHAR16*)
            L"virtio-net: probing device\n"
    );

    if (!virtio_common::probe_device(
            virtio_common::DeviceType::NETWORK,
            &g_device))
    {
        Print(
            (CHAR16*)
                L"virtio-net: device not found\n"
        );

        return false;
    }

    if (g_device.mmio)
    {
        Print(
            (CHAR16*)
                L"virtio-net: MMIO transport is not supported by this driver\n"
        );

        return false;
    }

    if (!initialize_device())
    {
        Print(
            (CHAR16*)
                L"virtio-net: device initialization failed\n"
        );

        write_status(
            STATUS_FAILED
        );

        return false;
    }

    if (!read_mac())
    {
        Print(
            (CHAR16*)
                L"virtio-net: invalid MAC address\n"
        );

        write_status(
            STATUS_FAILED
        );

        return false;
    }

    g_rx_queue_mem =
        dma::alloc(
            QUEUE_MEMORY_SIZE,
            4096
        );

    if (g_rx_queue_mem == nullptr)
    {
        Print(
            (CHAR16*)
                L"virtio-net: RX queue allocation failed\n"
        );

        return false;
    }

    g_tx_queue_mem =
        dma::alloc(
            QUEUE_MEMORY_SIZE,
            4096
        );

    if (g_tx_queue_mem == nullptr)
    {
        Print(
            (CHAR16*)
                L"virtio-net: TX queue allocation failed\n"
        );

        return false;
    }

    memset(
        g_rx_queue_mem,
        0,
        QUEUE_MEMORY_SIZE
    );

    memset(
        g_tx_queue_mem,
        0,
        QUEUE_MEMORY_SIZE
    );

    if (!setup_legacy_queue(
            RX_QUEUE,
            &g_rx_vq,
            g_rx_queue_mem,
            RX_QUEUE_SIZE))
    {
        Print(
            (CHAR16*)
                L"virtio-net: RX queue setup failed\n"
        );

        return false;
    }

    if (!setup_legacy_queue(
            TX_QUEUE,
            &g_tx_vq,
            g_tx_queue_mem,
            TX_QUEUE_SIZE))
    {
        Print(
            (CHAR16*)
                L"virtio-net: TX queue setup failed\n"
        );

        return false;
    }

    if (!initialize_rx_queue())
    {
        Print(
            (CHAR16*)
                L"virtio-net: RX descriptor initialization failed\n"
        );

        return false;
    }

    g_queues_ready = true;

    if (!start_device())
    {
        Print(
            (CHAR16*)
                L"virtio-net: DRIVER_OK failed\n"
        );

        return false;
    }

    notify_queue(
        RX_QUEUE
    );

    g_ready = true;
    g_initialized = true;

    Print(
        (CHAR16*)
            L"virtio-net: device ready, RX/TX queues active\n"
    );

    return true;
}


// ------------------------------------------------------------
// Availability
// ------------------------------------------------------------

bool is_available()
{
    return g_ready;
}


// ------------------------------------------------------------
// MAC address
// ------------------------------------------------------------

bool get_mac_address(
    uint8_t out_mac[6]
)
{
    if (out_mac == nullptr)
        return false;

    if (!g_initialized)
        return false;

    memcpy(
        out_mac,
        g_mac,
        6
    );

    return true;
}


// ------------------------------------------------------------
// TX
// ------------------------------------------------------------

bool send_packet(
    const void* data,
    unsigned len
)
{
    if (!g_ready)
        return false;

    if (!g_queues_ready)
        return false;

    if (data == nullptr || len == 0)
        return false;

    const size_t total =
        VIRTIO_NET_HDR_SIZE +
        static_cast<size_t>(len);

    if (total > RX_BUFFER_SIZE)
        return false;

    /*
     * Reclaim completed TX descriptors first.
     *
     * reclaim_tx() is provided by the VirtIO TX implementation.
     */
    reclaim_tx();

    for (uint32_t i = 0;
         i < TX_QUEUE_SIZE;
         ++i)
    {
        if (g_tx_slots[i].buf != nullptr)
            continue;

        void* packet =
            dma::alloc(
                total,
                4096
            );

        if (packet == nullptr)
            return false;

        memset(
            packet,
            0,
            VIRTIO_NET_HDR_SIZE
        );

        memcpy(
            static_cast<uint8_t*>(packet) +
                VIRTIO_NET_HDR_SIZE,
            data,
            len
        );

        const uint64_t address =
            static_cast<uint64_t>(
                reinterpret_cast<UINTN>(
                    packet
                )
            );

        virtqueue_ops::set_descriptor(
            &g_tx_vq,
            i,
            address,
            static_cast<uint32_t>(total),
            0,
            0
        );

        virtqueue_ops::submit_descriptor(
            &g_tx_vq,
            i
        );

        g_tx_slots[i].buf =
            packet;

        /*
         * The current TxSlot structure stores submit_tick.
         * No platform timer is required for the initial submission.
         */
        g_tx_slots[i].submit_tick = 0;

        notify_queue(
            TX_QUEUE
        );

        return true;
    }

    return false;
}

} // namespace virtio_net
