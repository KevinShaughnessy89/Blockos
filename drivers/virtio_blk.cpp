#include "virtio_blk.hpp"
#include "virtio_common.hpp"
#include "virtqueue.hpp"
#include "virtqueue_ops.hpp"
#include "dma.hpp"

extern "C" {
#include <efi.h>
}
extern "C" {
#include <efilib.h>
}

#include <string.h>
#include <stdint.h>
#include <stddef.h>

namespace {

static bool blk_ready = false;
static bool blk_flush_ok = false;
static uint64_t blk_capacity = 0;

static virtio_common::DeviceHandle g_handle{};
static VirtQueueView g_vq{};
static void* g_queue_mem = nullptr;
static uint32_t g_queue_size = 0;

static volatile uint32_t g_lock = 0;
static bool g_io_failed = false;

static void lock_queue()
{
    while (__atomic_test_and_set(&g_lock, __ATOMIC_ACQUIRE))
        __asm__ volatile("pause");
}

static void unlock_queue()
{
    __atomic_clear(&g_lock, __ATOMIC_RELEASE);
}

#pragma pack(push, 1)
struct VirtioBlkReq
{
    uint32_t type;
    uint32_t reserved;
    uint64_t sector;
};
#pragma pack(pop)

static constexpr uint32_t VIRTIO_BLK_T_IN    = 0;
static constexpr uint32_t VIRTIO_BLK_T_OUT   = 1;
static constexpr uint32_t VIRTIO_BLK_T_FLUSH = 4;

static constexpr uint16_t VIRTQ_DESC_F_NEXT  = 1;
static constexpr uint16_t VIRTQ_DESC_F_WRITE = 2;
static constexpr uint8_t  VIRTIO_BLK_S_OK = 0;

enum class WaitResult : uint8_t { OK, DEVICE_ERROR, TIMEOUT };

static WaitResult wait_request(uint32_t expected_desc, uint8_t* status)
{
    uint32_t used_id = 0;
    uint32_t used_len = 0;

    constexpr uint64_t TIMEOUT_LOOPS = 5000000ULL;

    for (uint64_t i = 0; i < TIMEOUT_LOOPS; ++i) {
        if (virtqueue_ops::try_dequeue_used(
                &g_vq, &used_id, &used_len)) {
            __asm__ volatile("mfence" ::: "memory");
            if (used_id != expected_desc)
                return WaitResult::DEVICE_ERROR;
            if (status && *status != VIRTIO_BLK_S_OK)
                return WaitResult::DEVICE_ERROR;
            return WaitResult::OK;
        }
        if ((i & 0xFFFu) == 0)
            __asm__ volatile("pause");
    }

    return WaitResult::TIMEOUT;
}

static bool submit_request(
    uint32_t type,
    uint64_t sector,
    void* data,
    uint32_t data_len,
    bool device_writes_data)
{
    if (!blk_ready)
        return false;

    if (g_queue_size < 2)
        return false;

    const bool has_data = data != nullptr && data_len != 0;

    void* header = dma::alloc(sizeof(VirtioBlkReq), 16);
    void* status = dma::alloc(1, 1);
    if (!header || !status) {
        if (header) dma::free(header);
        if (status) dma::free(status);
        return false;
    }

    VirtioBlkReq req{};
    req.type = type;
    req.reserved = 0;
    req.sector = sector;
    memcpy(header, &req, sizeof(req));
    *reinterpret_cast<uint8_t*>(status) = 0xFF;

    // The driver uses a single submission at a time. Descriptors 0..2 are
    // reused under the queue lock, which makes the synchronous backend safe
    // for the current VFS path.
    const uint32_t header_desc = 0;
    const uint32_t data_desc = 1;
    const uint32_t status_desc = 2;

    virtqueue_ops::set_descriptor(
        &g_vq,
        header_desc,
        reinterpret_cast<uint64_t>(header),
        sizeof(VirtioBlkReq),
        has_data ? VIRTQ_DESC_F_NEXT : VIRTQ_DESC_F_NEXT,
        status_desc
    );

    if (has_data) {
        uint16_t flags = VIRTQ_DESC_F_NEXT;
        if (device_writes_data)
            flags |= VIRTQ_DESC_F_WRITE;

        virtqueue_ops::set_descriptor(
            &g_vq,
            data_desc,
            reinterpret_cast<uint64_t>(data),
            data_len,
            flags,
            status_desc
        );

        virtqueue_ops::set_descriptor(
            &g_vq,
            status_desc,
            reinterpret_cast<uint64_t>(status),
            1,
            VIRTQ_DESC_F_WRITE,
            0
        );
    } else {
        // Header directly followed by status for FLUSH.
        virtqueue_ops::set_descriptor(
            &g_vq,
            header_desc,
            reinterpret_cast<uint64_t>(header),
            sizeof(VirtioBlkReq),
            VIRTQ_DESC_F_NEXT,
            status_desc
        );

        virtqueue_ops::set_descriptor(
            &g_vq,
            status_desc,
            reinterpret_cast<uint64_t>(status),
            1,
            VIRTQ_DESC_F_WRITE,
            0
        );
    }

    __asm__ volatile("mfence" ::: "memory");
    virtqueue_ops::submit_descriptor(&g_vq, header_desc);
    if (!virtio_common::notify_queue(&g_handle, 0)) {
        dma::free(header);
        dma::free(status);
        return false;
    }

    const WaitResult result =
        wait_request(header_desc, reinterpret_cast<uint8_t*>(status));

    if (result == WaitResult::TIMEOUT) {
        // Do not free DMA memory while the device might still own it.
        g_io_failed = true;
        blk_ready = false;
        virtio_common::set_device_status(&g_handle, 0x80);
        return false;
    }

    // Completion reached the used ring, so the device no longer owns the
    // request buffers and it is safe to release them.
    virtqueue_ops::set_descriptor(&g_vq, 0, 0, 0, 0, 0);
    if (g_queue_size > 1)
        virtqueue_ops::set_descriptor(&g_vq, 1, 0, 0, 0, 0);
    if (g_queue_size > 2)
        virtqueue_ops::set_descriptor(&g_vq, 2, 0, 0, 0, 0);

    dma::free(header);
    dma::free(status);
    return result == WaitResult::OK;
}

} // namespace

namespace virtio_blk {

bool init()
{
    if (blk_ready)
        return true;

    g_io_failed = false;

    if (!virtio_common::probe_device(
            virtio_common::DeviceType::BLOCK,
            &g_handle)) {
        Print((CHAR16*)L"virtio-blk: no PCI VirtIO block device\n");
        return false;
    }

    const uint64_t wanted = (1ULL << 9); // VIRTIO_BLK_F_FLUSH
    if (!virtio_common::device_init(&g_handle, wanted)) {
        Print((CHAR16*)L"virtio-blk: device initialization failed\n");
        return false;
    }

    const uint16_t max_q = virtio_common::queue_max_size(&g_handle, 0);
    if (max_q < 2) {
        Print((CHAR16*)L"virtio-blk: queue 0 is unavailable\n");
        virtio_common::set_device_status(&g_handle, 0x80);
        return false;
    }

    g_queue_size = max_q;
    if (g_queue_size > 128)
        g_queue_size = 128;

    const size_t desc_size = sizeof(VirtqDesc) * g_queue_size;
    const size_t avail_size = sizeof(uint16_t) * 2 + sizeof(uint16_t) * g_queue_size;
    const size_t used_offset = (desc_size + avail_size + 4095u) & ~size_t(4095u);
    const size_t used_size = sizeof(uint16_t) * 2 + sizeof(VirtqUsedElem) * g_queue_size;
    const size_t queue_mem_size = used_offset + used_size + 4096;

    g_queue_mem = dma::alloc(queue_mem_size, 4096);
    if (!g_queue_mem) {
        Print((CHAR16*)L"virtio-blk: queue DMA allocation failed\n");
        return false;
    }
    memset(g_queue_mem, 0, queue_mem_size);

    if (!virtio_common::setup_queue(
            &g_handle,
            0,
            g_queue_mem,
            g_queue_size,
            &g_vq)) {
        Print((CHAR16*)L"virtio-blk: queue setup failed\n");
        dma::free(g_queue_mem);
        g_queue_mem = nullptr;
        return false;
    }

    const uint8_t running_status =
        virtio_common::get_device_status(&g_handle) | 0x04;
    virtio_common::set_device_status(&g_handle, running_status);
    if (!(virtio_common::get_device_status(&g_handle) & 0x04)) {
        Print((CHAR16*)L"virtio-blk: DRIVER_OK rejected\n");
        dma::free(g_queue_mem);
        g_queue_mem = nullptr;
        return false;
    }

    blk_capacity = virtio_common::block_capacity_sectors(&g_handle);
    blk_flush_ok = virtio_common::block_flush_supported(&g_handle);
    blk_ready = true;

    Print((CHAR16*)L"virtio-blk: ready, sectors=%lu flush=%u queue=%u\n",
          (UINT64)blk_capacity,
          blk_flush_ok ? 1 : 0,
          (UINTN)g_queue_size);
    return true;
}

bool is_ready()
{
    return blk_ready;
}

uint64_t capacity_sectors()
{
    return blk_capacity;
}

bool read_sector(uint64_t sector, uint8_t* out_buf)
{
    return read_sectors(sector, 1, out_buf);
}

bool write_sector(uint64_t sector, const uint8_t* in_buf)
{
    return write_sectors(sector, 1, in_buf);
}

bool read_sectors(uint64_t sector, uint32_t count, uint8_t* out_buf)
{
    if (!out_buf || count == 0 || !blk_ready)
        return false;
    if (blk_capacity && ((uint64_t)count > blk_capacity || sector >= blk_capacity - count + 1))
        return false;

    uint32_t done = 0;
    while (done < count) {
        const uint32_t chunk = (count - done > 2048u) ? 2048u : (count - done);
        const uint32_t bytes = chunk * 512u;

        void* dma_buf = dma::alloc(bytes, 4096);
        if (!dma_buf)
            return false;

        lock_queue();
        const bool ok = submit_request(
            VIRTIO_BLK_T_IN,
            sector + done,
            dma_buf,
            bytes,
            true);
        unlock_queue();

        if (ok)
            memcpy(out_buf + (size_t)done * 512u, dma_buf, bytes);
        if (!g_io_failed)
            dma::free(dma_buf);

        if (!ok)
            return false;
        done += chunk;
    }
    return true;
}

bool write_sectors(uint64_t sector, uint32_t count, const uint8_t* in_buf)
{
    if (!in_buf || count == 0 || !blk_ready)
        return false;
    if (blk_capacity && ((uint64_t)count > blk_capacity || sector >= blk_capacity - count + 1))
        return false;

    uint32_t done = 0;
    while (done < count) {
        const uint32_t chunk = (count - done > 2048u) ? 2048u : (count - done);
        const uint32_t bytes = chunk * 512u;

        void* dma_buf = dma::alloc(bytes, 4096);
        if (!dma_buf)
            return false;
        memcpy(dma_buf, in_buf + (size_t)done * 512u, bytes);

        lock_queue();
        const bool ok = submit_request(
            VIRTIO_BLK_T_OUT,
            sector + done,
            dma_buf,
            bytes,
            false);
        unlock_queue();

        if (!g_io_failed)
            dma::free(dma_buf);
        if (!ok)
            return false;
        done += chunk;
    }

    return true;
}

bool flush()
{
    if (!blk_ready)
        return false;
    if (!blk_flush_ok)
        return true; // Device guarantees no explicit FLUSH operation was advertised.

    lock_queue();
    const bool ok = submit_request(
        VIRTIO_BLK_T_FLUSH,
        0,
        nullptr,
        0,
        false);
    unlock_queue();
    return ok;
}

} // namespace virtio_blk
