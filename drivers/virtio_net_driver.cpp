#include "virtio_net_driver.hpp"
#include "virtio_net_internal.hpp"
#include "virtio_common.hpp"
#include "virtqueue.hpp"
#include "virtqueue_ops.hpp"
#include "dma.hpp"
#include "virtio_notify.hpp"
#include "virtio_service.hpp"

extern "C" {
#include <efi.h>
}

extern "C" {
#include <efilib.h>
}

#include <string.h>
#include <stdint.h>

namespace {

static virtio_common::DeviceHandle g_device{};

static void* g_tx_mem = nullptr;
static void* g_rx_mem = nullptr;

static const uint32_t TX_QUEUE_SIZE = 128;
static const uint32_t RX_QUEUE_SIZE = 128;

// VirtIO legacy network device configuration.
//
// BAR0 + 0x14 is the beginning of device-specific
// configuration space for the legacy VirtIO PCI device.
//
// VIRTIO_NET_F_MAC means the first 6 bytes contain
// the device MAC address.
static const uint32_t VIRTIO_NET_CONFIG_OFFSET = 0x14;

static uint8_t g_mac[6] = {
    0, 0, 0, 0, 0, 0
};

static bool g_mac_valid = false;


// ------------------------------------------------------------
// Legacy PCI I/O helpers
// ------------------------------------------------------------

static uint8_t io_in8(uint16_t port)
{
    uint8_t value;

    __asm__ volatile(
        "inb %1, %0"
        : "=a"(value)
        : "dN"(port)
    );

    return value;
}


// ------------------------------------------------------------
// Read legacy VirtIO configuration byte
// ------------------------------------------------------------

static uint8_t read_device_config8(
    uint32_t offset
)
{
    if (g_device.bar0 == 0)
        return 0;

    if (g_device.mmio)
    {
        volatile uint8_t* p =
            reinterpret_cast<volatile uint8_t*>(
                static_cast<UINTN>(
                    g_device.bar0 + offset
                )
            );

        return *p;
    }

    return io_in8(
        static_cast<uint16_t>(
            g_device.bar0 + offset
        )
    );
}


// ------------------------------------------------------------
// MAC validity check
// ------------------------------------------------------------

static bool valid_mac(
    const uint8_t mac[6]
)
{
    if (mac == nullptr)
        return false;

    bool all_zero = true;
    bool all_ff = true;

    for (unsigned i = 0; i < 6; ++i)
    {
        if (mac[i] != 0)
            all_zero = false;

        if (mac[i] != 0xFF)
            all_ff = false;
    }

    if (all_zero)
        return false;

    if (all_ff)
        return false;

    // Multicast addresses cannot be used as the
    // interface's normal unicast MAC.
    if (mac[0] & 0x01)
        return false;

    return true;
}


// ------------------------------------------------------------
// Read MAC from VirtIO device configuration
// ------------------------------------------------------------

static bool read_mac_from_device()
{
    uint8_t mac[6];

    for (unsigned i = 0; i < 6; ++i)
    {
        mac[i] =
            read_device_config8(
                VIRTIO_NET_CONFIG_OFFSET + i
            );
    }

    if (!valid_mac(mac))
    {
        Print(
            (const CHAR16*)
            L"virtio-net: invalid MAC address\n"
        );

        return false;
    }

    memcpy(
        g_mac,
        mac,
        sizeof(g_mac)
    );

    g_mac_valid = true;

    CHAR16 msg[128];

    UnicodeSPrint(
        msg,
        sizeof(msg),
        (const CHAR16*)
        L"virtio-net: MAC %02x:%02x:%02x:%02x:%02x:%02x\n",
        g_mac[0],
        g_mac[1],
        g_mac[2],
        g_mac[3],
        g_mac[4],
        g_mac[5]
    );

    Print(msg);

    return true;
}


// ------------------------------------------------------------
// TX slot cleanup
// ------------------------------------------------------------

static void clear_tx_slot(
    uint32_t id
)
{
    if (id >= TX_QUEUE_SIZE)
        return;

    virtio_net::g_tx_slots[id].buf =
        nullptr;

    virtio_net::g_tx_slots[id].submit_tick =
        0;
}


// ------------------------------------------------------------
// Console helper
// ------------------------------------------------------------

static void print_message(
    const CHAR16* message
)
{
    Print(message);
}

} // anonymous namespace


namespace virtio_net {

// ------------------------------------------------------------
// Global driver state
// ------------------------------------------------------------

bool g_ready = false;

VirtQueueView g_tx_vq{};
VirtQueueView g_rx_vq{};

TxSlot g_tx_slots[256]{};


// ------------------------------------------------------------
// TX pool
// ------------------------------------------------------------

void tx_pool_push(
    void* buf
)
{
    if (buf == nullptr)
        return;

    /*
     * Current BlockOS DMA allocator does not yet expose
     * a matching free operation here.
     *
     * Keep the buffer tracked until the DMA memory manager
     * gains recycling support.
     */
    (void)buf;
}


// ------------------------------------------------------------
// Get MAC
// ------------------------------------------------------------

bool get_mac_address(
    uint8_t out_mac[6]
)
{
    if (out_mac == nullptr)
        return false;

    if (!g_mac_valid)
        return false;

    memcpy(
        out_mac,
        g_mac,
        6
    );

    return true;
}


// ------------------------------------------------------------
// Initialize VirtIO network
// ------------------------------------------------------------

bool init()
{
    g_ready = false;
    g_mac_valid = false;

    memset(
        g_mac,
        0,
        sizeof(g_mac)
    );

    print_message(
        (const CHAR16*)
        L"virtio-net: initializing\n"
    );

    // --------------------------------------------------------
    // Clear driver state
    // --------------------------------------------------------

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

    // --------------------------------------------------------
    // Find VirtIO network device
    // --------------------------------------------------------

    if (!virtio_common::probe_device(
            virtio_common::DeviceType::NETWORK,
            &g_device))
    {
        print_message(
            (const CHAR16*)
            L"virtio-net: VirtIO network device not found\n"
        );

        return false;
    }

    print_message(
        (const CHAR16*)
        L"virtio-net: device found\n"
    );

    // --------------------------------------------------------
    // STEP 1
    //
    // Device reset + acknowledge + driver state +
    // feature negotiation.
    // --------------------------------------------------------

    if (!virtio_common::device_init(
            &g_device))
    {
        print_message(
            (const CHAR16*)
            L"virtio-net: common device initialization failed\n"
        );

        return false;
    }

    print_message(
        (const CHAR16*)
        L"virtio-net: device initialization OK\n"
    );

    // --------------------------------------------------------
    // STEP 2
    //
    // Read hardware MAC address.
    // --------------------------------------------------------

    if (!read_mac_from_device())
    {
        print_message(
            (const CHAR16*)
            L"virtio-net: MAC read failed\n"
        );

        return false;
    }

    print_message(
        (const CHAR16*)
        L"virtio-net: MAC address available\n"
    );

    // --------------------------------------------------------
    // Queue memory
    // --------------------------------------------------------

    const size_t QUEUE_MEM_SIZE =
        16384;

    g_tx_mem =
        dma::alloc(
            QUEUE_MEM_SIZE,
            4096
        );

    if (g_tx_mem == nullptr)
    {
        print_message(
            (const CHAR16*)
            L"virtio-net: TX queue DMA allocation failed\n"
        );

        return false;
    }

    g_rx_mem =
        dma::alloc(
            QUEUE_MEM_SIZE,
            4096
        );

    if (g_rx_mem == nullptr)
    {
        print_message(
            (const CHAR16*)
            L"virtio-net: RX queue DMA allocation failed\n"
        );

        return false;
    }

    memset(
        g_tx_mem,
        0,
        QUEUE_MEM_SIZE
    );

    memset(
        g_rx_mem,
        0,
        QUEUE_MEM_SIZE
    );

    // --------------------------------------------------------
    // Create queue views
    // --------------------------------------------------------

    g_tx_vq =
        virtqueue_ops::view_from_mem(
            g_tx_mem,
            TX_QUEUE_SIZE
        );

    g_rx_vq =
        virtqueue_ops::view_from_mem(
            g_rx_mem,
            RX_QUEUE_SIZE
        );

    if (g_tx_vq.desc == nullptr ||
        g_tx_vq.avail == nullptr ||
        g_tx_vq.used == nullptr)
    {
        print_message(
            (const CHAR16*)
            L"virtio-net: TX queue view creation failed\n"
        );

        return false;
    }

    if (g_rx_vq.desc == nullptr ||
        g_rx_vq.avail == nullptr ||
        g_rx_vq.used == nullptr)
    {
        print_message(
            (const CHAR16*)
            L"virtio-net: RX queue view creation failed\n"
        );

        return false;
    }

    // --------------------------------------------------------
    // Initialize queue rings
    // --------------------------------------------------------

    virtqueue_ops::init_rings(
        &g_tx_vq
    );

    virtqueue_ops::init_rings(
        &g_rx_vq
    );

    print_message(
        (const CHAR16*)
        L"virtio-net: virtqueues initialized\n"
    );

    /*
     * IMPORTANT:
     *
     * DRIVER_OK is still NOT set here.
     *
     * Step 3:
     *   - queue selection
     *   - queue address programming
     *   - RX buffers
     *   - RX descriptors
     *
     * Step 4:
     *   - TX notification
     *   - RX notification
     *   - DRIVER_OK
     *
     * must happen before g_ready becomes true.
     */

    g_ready = false;

    print_message(
        (const CHAR16*)
        L"virtio-net: MAC ready, waiting for queue setup\n"
    );

    return true;
}


// ------------------------------------------------------------
// Driver available?
// ------------------------------------------------------------

bool is_available()
{
    return g_ready;
}


// ------------------------------------------------------------
// Send packet
// ------------------------------------------------------------

bool send_packet(
    const void* data,
    unsigned len
)
{
    if (!g_ready)
        return false;

    if (data == nullptr ||
        len == 0)
        return false;

    if (g_tx_vq.size == 0 ||
        g_tx_vq.desc == nullptr)
        return false;

    reclaim_tx();

    uint32_t slot_count =
        TX_QUEUE_SIZE;

    if (g_tx_vq.size < slot_count)
        slot_count = g_tx_vq.size;

    for (uint32_t i = 0;
         i < slot_count;
         ++i)
    {
        if (g_tx_slots[i].buf != nullptr)
            continue;

        void* tx_buf =
            dma::alloc(
                len,
                4096
            );

        if (tx_buf == nullptr)
        {
            print_message(
                (const CHAR16*)
                L"virtio-net: TX buffer allocation failed\n"
            );

            return false;
        }

        memcpy(
            tx_buf,
            data,
            len
        );

        const uint64_t addr =
            static_cast<uint64_t>(
                reinterpret_cast<UINTN>(
                    tx_buf
                )
            );

        virtqueue_ops::set_descriptor(
            &g_tx_vq,
            i,
            addr,
            static_cast<uint32_t>(len),
            0,
            0
        );

        virtqueue_ops::submit_descriptor(
            &g_tx_vq,
            i
        );

        g_tx_slots[i].buf =
            tx_buf;

        g_tx_slots[i].submit_tick =
            virtio_service::now_ticks();

        /*
         * Device notification will be added in the
         * VirtIO queue/notify stage.
         */

        return true;
    }

    print_message(
        (const CHAR16*)
        L"virtio-net: no free TX descriptor\n"
    );

    return false;
}


// ------------------------------------------------------------
// Receive packet
// ------------------------------------------------------------

int receive_packet(
    void* buf,
    unsigned buf_len
)
{
    if (!g_ready)
        return -1;

    if (buf == nullptr ||
        buf_len == 0)
        return -1;

    if (g_rx_vq.size == 0 ||
        g_rx_vq.desc == nullptr)
        return 0;

    uint32_t id = 0;
    uint32_t len = 0;

    if (!virtqueue_ops::try_dequeue_used(
            &g_rx_vq,
            &id,
            &len))
    {
        return 0;
    }

    if (id >= g_rx_vq.size)
        return -1;

    const uint64_t addr =
        g_rx_vq.desc[id].addr;

    if (addr == 0)
        return -1;

    void* rx_buf =
        reinterpret_cast<void*>(
            static_cast<UINTN>(addr)
        );

    unsigned copy_len = len;

    if (copy_len > buf_len)
        copy_len = buf_len;

    memcpy(
        buf,
        rx_buf,
        copy_len
    );

    virtqueue_ops::set_descriptor(
        &g_rx_vq,
        id,
        0,
        0,
        0,
        0
    );

    return static_cast<int>(
        copy_len
    );
}


// ------------------------------------------------------------
// Reclaim TX
// ------------------------------------------------------------

void reclaim_tx()
{
    if (!g_ready)
        return;

    uint32_t id = 0;
    uint32_t len = 0;

    while (virtqueue_ops::try_dequeue_used(
        &g_tx_vq,
        &id,
        &len))
    {
        (void)len;

        if (id >= g_tx_vq.size)
            continue;

        void* tracked_buf = nullptr;

        if (id < TX_QUEUE_SIZE)
        {
            tracked_buf =
                g_tx_slots[id].buf;
        }

        const uint64_t addr =
            g_tx_vq.desc[id].addr;

        void* descriptor_buf =
            reinterpret_cast<void*>(
                static_cast<UINTN>(addr)
            );

        virtqueue_ops::set_descriptor(
            &g_tx_vq,
            id,
            0,
            0,
            0,
            0
        );

        if (tracked_buf != nullptr)
        {
            tx_pool_push(
                tracked_buf
            );

            clear_tx_slot(
                id
            );
        }
        else if (descriptor_buf != nullptr)
        {
            tx_pool_push(
                descriptor_buf
            );
        }
    }

    // --------------------------------------------------------
    // TX timeout recovery
    // --------------------------------------------------------

    const uint64_t now =
        virtio_service::now_ticks();

    static const uint64_t TIMEOUT_TICKS = 20;

    uint32_t max_slots =
        TX_QUEUE_SIZE;

    if (g_tx_vq.size < max_slots)
        max_slots = g_tx_vq.size;

    for (uint32_t i = 0;
         i < max_slots;
         ++i)
    {
        if (g_tx_slots[i].buf == nullptr)
            continue;

        const uint64_t submitted =
            g_tx_slots[i].submit_tick;

        if (now < submitted)
            continue;

        if ((now - submitted) <= TIMEOUT_TICKS)
            continue;

        CHAR16 msg[128];

        UnicodeSPrint(
            msg,
            sizeof(msg),
            (const CHAR16*)
            L"virtio-net: TX timeout slot %u\n",
            i
        );

        Print(msg);

        void* pending =
            g_tx_slots[i].buf;

        if (pending != nullptr)
        {
            tx_pool_push(
                pending
            );
        }

        clear_tx_slot(
            i
        );

        virtqueue_ops::set_descriptor(
            &g_tx_vq,
            i,
            0,
            0,
            0,
            0
        );
    }
}

} // namespace virtio_net
