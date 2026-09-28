#include "virtio_net_driver.hpp"
#include "virtio_net_internal.hpp"
#include "virtio_common.hpp"
#include "virtqueue_ops.hpp"
#include "dma.hpp"

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

constexpr uint16_t RX_QUEUE = 0;
constexpr uint16_t TX_QUEUE = 1;
constexpr uint16_t RX_QUEUE_REQUESTED = 128;
constexpr uint16_t TX_QUEUE_REQUESTED = 128;
constexpr uint32_t RX_BUFFER_SIZE = 2048;
constexpr uint32_t VIRTIO_NET_HDR_SIZE = 10;
constexpr uint64_t VIRTIO_NET_F_MAC = 1ULL << 5;
constexpr uint8_t STATUS_DRIVER_OK = 0x04;
constexpr uint8_t STATUS_FAILED = 0x80;

static virtio_common::DeviceHandle g_device{};
static void* g_rx_queue_mem = nullptr;
static void* g_tx_queue_mem = nullptr;
static void* g_rx_buffers[256]{};
static uint16_t g_rx_size = 0;
static uint16_t g_tx_size = 0;
static uint8_t g_mac[6]{};
static bool g_initialized = false;
static volatile uint32_t g_lock = 0;

static void lock_net()
{
    while (__atomic_test_and_set(&g_lock, __ATOMIC_ACQUIRE))
        __asm__ volatile("pause");
}

static void unlock_net()
{
    __atomic_clear(&g_lock, __ATOMIC_RELEASE);
}

static size_t queue_memory_size(uint16_t qsize)
{
    const size_t desc_size = sizeof(VirtqDesc) * qsize;
    const size_t avail_size = sizeof(uint16_t) * 2 + sizeof(uint16_t) * qsize;
    const size_t used_offset = (desc_size + avail_size + 4095u) & ~size_t(4095u);
    const size_t used_size = sizeof(uint16_t) * 2 + sizeof(VirtqUsedElem) * qsize;
    return used_offset + used_size + 4096;
}

static bool prepare_rx_desc(uint16_t i)
{
    if (i >= g_rx_size)
        return false;

    void* buffer = g_rx_buffers[i];
    if (!buffer) {
        buffer = dma::alloc(RX_BUFFER_SIZE, 4096);
        if (!buffer)
            return false;
        g_rx_buffers[i] = buffer;
    }

    memset(buffer, 0, RX_BUFFER_SIZE);
    virtqueue_ops::set_descriptor(
        &virtio_net::g_rx_vq,
        i,
        reinterpret_cast<uint64_t>(buffer),
        RX_BUFFER_SIZE,
        2, // VIRTQ_DESC_F_WRITE
        0
    );
    virtqueue_ops::submit_descriptor(&virtio_net::g_rx_vq, i);
    return true;
}

static bool read_mac()
{
    for (unsigned i = 0; i < 6; ++i)
        g_mac[i] = virtio_common::read_device_config8(&g_device, i);

    bool all_zero = true;
    bool all_ff = true;
    for (unsigned i = 0; i < 6; ++i) {
        if (g_mac[i] != 0) all_zero = false;
        if (g_mac[i] != 0xFF) all_ff = false;
    }

    if (all_zero || all_ff || (g_mac[0] & 1))
        return false;
    return true;
}

static void fail_device()
{
    virtio_common::set_device_status(&g_device, STATUS_FAILED);
}

} // namespace

namespace virtio_net {

bool g_ready = false;
VirtQueueView g_tx_vq{};
VirtQueueView g_rx_vq{};
TxSlot g_tx_slots[256]{};

void tx_pool_push(void* buf)
{
    if (buf)
        dma::free(buf);
}

bool init()
{
    if (g_initialized)
        return g_ready;

    lock_net();
    if (g_initialized) {
        unlock_net();
        return g_ready;
    }

    g_ready = false;
    memset(&g_device, 0, sizeof(g_device));
    memset(&g_tx_vq, 0, sizeof(g_tx_vq));
    memset(&g_rx_vq, 0, sizeof(g_rx_vq));
    memset(g_tx_slots, 0, sizeof(g_tx_slots));
    memset(g_rx_buffers, 0, sizeof(g_rx_buffers));

    if (!virtio_common::probe_device(
            virtio_common::DeviceType::NETWORK,
            &g_device)) {
        Print((CHAR16*)L"virtio-net: no VirtIO PCI network device\n");
        unlock_net();
        return false;
    }

    if (!virtio_common::device_init(&g_device, VIRTIO_NET_F_MAC)) {
        Print((CHAR16*)L"virtio-net: feature negotiation failed\n");
        fail_device();
        unlock_net();
        return false;
    }

    if (!read_mac()) {
        Print((CHAR16*)L"virtio-net: invalid device MAC\n");
        fail_device();
        unlock_net();
        return false;
    }

    const uint16_t rx_max = virtio_common::queue_max_size(&g_device, RX_QUEUE);
    const uint16_t tx_max = virtio_common::queue_max_size(&g_device, TX_QUEUE);
    if (rx_max < 2 || tx_max < 2) {
        Print((CHAR16*)L"virtio-net: RX/TX queue unavailable\n");
        fail_device();
        unlock_net();
        return false;
    }

    g_rx_size = rx_max > RX_QUEUE_REQUESTED ? RX_QUEUE_REQUESTED : rx_max;
    g_tx_size = tx_max > TX_QUEUE_REQUESTED ? TX_QUEUE_REQUESTED : tx_max;
    if (g_rx_size > 256) g_rx_size = 256;
    if (g_tx_size > 256) g_tx_size = 256;

    const size_t rx_bytes = queue_memory_size(g_rx_size);
    const size_t tx_bytes = queue_memory_size(g_tx_size);
    g_rx_queue_mem = dma::alloc(rx_bytes, 4096);
    g_tx_queue_mem = dma::alloc(tx_bytes, 4096);
    if (!g_rx_queue_mem || !g_tx_queue_mem) {
        Print((CHAR16*)L"virtio-net: queue memory allocation failed\n");
        fail_device();
        if (g_rx_queue_mem) dma::free(g_rx_queue_mem);
        if (g_tx_queue_mem) dma::free(g_tx_queue_mem);
        g_rx_queue_mem = g_tx_queue_mem = nullptr;
        unlock_net();
        return false;
    }
    memset(g_rx_queue_mem, 0, rx_bytes);
    memset(g_tx_queue_mem, 0, tx_bytes);

    if (!virtio_common::setup_queue(
            &g_device, RX_QUEUE, g_rx_queue_mem, g_rx_size, &g_rx_vq) ||
        !virtio_common::setup_queue(
            &g_device, TX_QUEUE, g_tx_queue_mem, g_tx_size, &g_tx_vq)) {
        Print((CHAR16*)L"virtio-net: queue setup failed\n");
        fail_device();
        unlock_net();
        return false;
    }

    for (uint16_t i = 0; i < g_rx_size; ++i) {
        if (!prepare_rx_desc(i)) {
            Print((CHAR16*)L"virtio-net: RX buffer setup failed\n");
            fail_device();
            unlock_net();
            return false;
        }
    }

    __asm__ volatile("mfence" ::: "memory");
    const uint8_t status = virtio_common::get_device_status(&g_device) | STATUS_DRIVER_OK;
    virtio_common::set_device_status(&g_device, status);
    if ((virtio_common::get_device_status(&g_device) & (STATUS_DRIVER_OK | STATUS_FAILED)) != STATUS_DRIVER_OK) {
        Print((CHAR16*)L"virtio-net: DRIVER_OK rejected\n");
        unlock_net();
        return false;
    }

    virtio_common::notify_queue(&g_device, RX_QUEUE);
    g_ready = true;
    g_initialized = true;

    Print((CHAR16*)L"virtio-net: ready, MAC %02x:%02x:%02x:%02x:%02x:%02x RX=%u TX=%u\n",
          (UINTN)g_mac[0], (UINTN)g_mac[1], (UINTN)g_mac[2],
          (UINTN)g_mac[3], (UINTN)g_mac[4], (UINTN)g_mac[5],
          (UINTN)g_rx_size, (UINTN)g_tx_size);

    unlock_net();
    return true;
}

bool is_available()
{
    return g_ready;
}

bool get_mac_address(uint8_t out_mac[6])
{
    if (!out_mac || !g_ready)
        return false;
    memcpy(out_mac, g_mac, 6);
    return true;
}

bool send_packet(const void* data, unsigned len)
{
    if (!g_ready || !data || len == 0 || len > 1514)
        return false;

    lock_net();
    reclaim_tx();

    const size_t total = VIRTIO_NET_HDR_SIZE + len;
    for (uint16_t i = 0; i < g_tx_size; ++i) {
        if (g_tx_slots[i].buf)
            continue;

        void* packet = dma::alloc(total, 4096);
        if (!packet) {
            unlock_net();
            return false;
        }

        memset(packet, 0, VIRTIO_NET_HDR_SIZE);
        memcpy(static_cast<uint8_t*>(packet) + VIRTIO_NET_HDR_SIZE, data, len);
        virtqueue_ops::set_descriptor(
            &g_tx_vq,
            i,
            reinterpret_cast<uint64_t>(packet),
            static_cast<uint32_t>(total),
            0,
            0);
        virtqueue_ops::submit_descriptor(&g_tx_vq, i);
        g_tx_slots[i].buf = packet;
        g_tx_slots[i].submit_tick = 0;
        __asm__ volatile("mfence" ::: "memory");
        const bool notified = virtio_common::notify_queue(&g_device, TX_QUEUE);
        unlock_net();
        if (!notified) {
            tx_pool_push(packet);
            g_tx_slots[i].buf = nullptr;
            return false;
        }
        return true;
    }

    unlock_net();
    return false;
}

int receive_packet(void* buf, unsigned buf_len)
{
    if (!g_ready || !buf || buf_len == 0)
        return 0;

    lock_net();

    uint32_t id = 0;
    uint32_t used_len = 0;
    if (!virtqueue_ops::try_dequeue_used(&g_rx_vq, &id, &used_len)) {
        unlock_net();
        return 0;
    }

    if (id >= g_rx_size || id >= 256 || !g_rx_buffers[id]) {
        unlock_net();
        return 0;
    }

    if (used_len < VIRTIO_NET_HDR_SIZE) {
        prepare_rx_desc(static_cast<uint16_t>(id));
        virtio_common::notify_queue(&g_device, RX_QUEUE);
        unlock_net();
        return 0;
    }

    const uint32_t payload_len = used_len - VIRTIO_NET_HDR_SIZE;
    const uint32_t copy_len = payload_len > buf_len ? buf_len : payload_len;
    memcpy(buf,
           static_cast<uint8_t*>(g_rx_buffers[id]) + VIRTIO_NET_HDR_SIZE,
           copy_len);

    // Requeue the same RX buffer immediately. The device writes into it only
    // after ownership is returned through the available ring.
    memset(g_rx_buffers[id], 0, RX_BUFFER_SIZE);
    virtqueue_ops::set_descriptor(
        &g_rx_vq,
        id,
        reinterpret_cast<uint64_t>(g_rx_buffers[id]),
        RX_BUFFER_SIZE,
        2,
        0);
    virtqueue_ops::submit_descriptor(&g_rx_vq, id);
    __asm__ volatile("mfence" ::: "memory");
    virtio_common::notify_queue(&g_device, RX_QUEUE);

    unlock_net();
    return static_cast<int>(copy_len);
}

void reclaim_tx()
{
    if (!g_ready)
        return;

    uint32_t id = 0;
    uint32_t len = 0;
    unsigned guard = 0;
    while (guard++ < 256 && virtqueue_ops::try_dequeue_used(&g_tx_vq, &id, &len)) {
        if (id >= g_tx_size || id >= 256)
            continue;
        void* buf = g_tx_slots[id].buf;
        g_tx_slots[id].buf = nullptr;
        g_tx_slots[id].submit_tick = 0;
        virtqueue_ops::set_descriptor(&g_tx_vq, id, 0, 0, 0, 0);
        if (buf)
            dma::free(buf);
    }
}

} // namespace virtio_net
