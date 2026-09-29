#include "nvme.hpp"
#include "pci.hpp"
#include "dma.hpp"
#include "device_manager.hpp"

extern "C" {
#include <efi.h>
#include <efilib.h>
}

#include <stdint.h>
#include <stddef.h>
#include <string.h>

extern "C" bool blockos_storage_register_backend(
    const char* name,
    bool (*read)(uint64_t, uint32_t, void*),
    bool (*write)(uint64_t, uint32_t, const void*),
    bool (*flush)(),
    bool (*erase_block)(uint64_t, uint32_t),
    uint64_t sectors,
    uint32_t sector_size,
    uint32_t erase_block_sectors,
    bool flash,
    bool controller_wear_levelled,
    bool preferred);

namespace nvme {
namespace {

constexpr uint16_t NVME_VENDOR_ANY = 0xFFFF;
constexpr uint8_t PCI_CLASS_STORAGE = 0x01;
constexpr uint8_t PCI_SUBCLASS_NVM = 0x08;
constexpr uint8_t PCI_PROGIF_NVM = 0x02;

constexpr uint32_t CAP = 0x00;
constexpr uint32_t VS = 0x08;
constexpr uint32_t INTMS = 0x0C;
constexpr uint32_t CC = 0x14;
constexpr uint32_t CSTS = 0x1C;
constexpr uint32_t AQA = 0x24;
constexpr uint32_t ASQ = 0x28;
constexpr uint32_t ACQ = 0x30;
constexpr uint32_t DOORBELL_BASE = 0x1000;

constexpr uint32_t CC_EN = 1u << 0;
constexpr uint32_t CSTS_RDY = 1u << 0;
constexpr uint32_t CSTS_CFS = 1u << 1;

constexpr uint8_t ADMIN_IDENTIFY = 0x06;
constexpr uint8_t ADMIN_CREATE_IO_CQ = 0x05;
constexpr uint8_t ADMIN_CREATE_IO_SQ = 0x01;
constexpr uint8_t ADMIN_GET_LOG_PAGE = 0x02;
constexpr uint8_t ADMIN_SET_FEATURES = 0x09;
constexpr uint8_t ADMIN_AER = 0x0C;

constexpr uint8_t IO_FLUSH = 0x00;
constexpr uint8_t IO_WRITE = 0x01;
constexpr uint8_t IO_READ = 0x02;

constexpr uint8_t IDENTIFY_CONTROLLER = 1;
constexpr uint8_t IDENTIFY_NAMESPACE = 0;

constexpr uint16_t QUEUE_DEPTH = 32;
constexpr uint32_t PAGE_SIZE = 4096;
constexpr uint32_t DMA_TIMEOUT = 2500000;

struct NvmeCommand {
    uint32_t cdw0;
    uint32_t nsid;
    uint32_t cdw2;
    uint32_t cdw3;
    uint64_t mptr;
    uint64_t prp1;
    uint64_t prp2;
    uint32_t cdw10;
    uint32_t cdw11;
    uint32_t cdw12;
    uint32_t cdw13;
    uint32_t cdw14;
    uint32_t cdw15;
};
static_assert(sizeof(NvmeCommand) == 64, "NvmeCommand must be 64 bytes");

struct NvmeCompletion {
    uint32_t dw0;
    uint32_t dw1;
    uint16_t sq_head;
    uint16_t sq_id;
    uint16_t command_id;
    uint16_t status;
};
static_assert(sizeof(NvmeCompletion) == 16, "NvmeCompletion must be 16 bytes");

struct State {
    bool ready = false;
    bool registered = false;
    uint8_t bus = 0;
    uint8_t slot = 0;
    uint8_t func = 0;
    uint16_t vendor = NVME_VENDOR_ANY;
    uint16_t device = 0;
    uint64_t bar0 = 0;
    uint64_t cap = 0;
    uint32_t version = 0;
    uint32_t doorbell_stride = 4;
    uint16_t admin_sq_tail = 0;
    uint16_t admin_cq_head = 0;
    uint16_t io_sq_tail = 0;
    uint16_t io_cq_head = 0;
    uint16_t next_cid = 1;
    uint16_t io_queue_id = 1;
    uint32_t error_count = 0;
    uint32_t last_status = 0;
    uint32_t nsid = 1;
    uint64_t namespace_sectors = 0;
    uint32_t sector_bytes = 512;
    void* admin_sq = nullptr;
    void* admin_cq = nullptr;
    void* io_sq = nullptr;
    void* io_cq = nullptr;
    void* scratch = nullptr;
    char model[41]{};
};

State g{};

static inline volatile uint8_t* mmio8(uint64_t off)
{
    return reinterpret_cast<volatile uint8_t*>(
        static_cast<uintptr_t>(g.bar0 + off));
}

static inline volatile uint32_t* mmio32(uint64_t off)
{
    return reinterpret_cast<volatile uint32_t*>(
        static_cast<uintptr_t>(g.bar0 + off));
}

static inline volatile uint64_t* mmio64(uint64_t off)
{
    return reinterpret_cast<volatile uint64_t*>(
        static_cast<uintptr_t>(g.bar0 + off));
}

static inline uint32_t rd32(uint64_t off)
{
    return *mmio32(off);
}

static inline uint64_t rd64(uint64_t off)
{
    return *mmio64(off);
}

static inline void wr32(uint64_t off, uint32_t v)
{
    *mmio32(off) = v;
}

static inline void wr64(uint64_t off, uint64_t v)
{
    *mmio64(off) = v;
}

static uint64_t pci_find_bar0(
    uint8_t* out_bus,
    uint8_t* out_slot,
    uint8_t* out_func,
    uint16_t* out_vendor,
    uint16_t* out_device)
{
    for (uint32_t bus = 0; bus < 256; ++bus) {
        for (uint8_t slot = 0; slot < 32; ++slot) {
            const uint16_t vendor0 = pci_cfg_read16((uint8_t)bus, slot, 0, 0x00);
            if (vendor0 == 0xFFFF)
                continue;

            uint8_t functions = 1;
            const uint8_t header = pci_cfg_read8((uint8_t)bus, slot, 0, 0x0E);
            if (header & 0x80)
                functions = 8;

            for (uint8_t func = 0; func < functions; ++func) {
                const uint16_t vendor = pci_cfg_read16((uint8_t)bus, slot, func, 0x00);
                if (vendor == 0xFFFF)
                    continue;

                const uint16_t device = pci_cfg_read16((uint8_t)bus, slot, func, 0x02);
                const uint8_t class_id = pci_cfg_read8((uint8_t)bus, slot, func, 0x0B);
                const uint8_t subclass = pci_cfg_read8((uint8_t)bus, slot, func, 0x0A);
                const uint8_t prog_if = pci_cfg_read8((uint8_t)bus, slot, func, 0x09);

                if (class_id != PCI_CLASS_STORAGE ||
                    subclass != PCI_SUBCLASS_NVM ||
                    prog_if != PCI_PROGIF_NVM)
                    continue;

                uint64_t bar = pci_read_bar((uint8_t)bus, slot, func, 0);
                if (bar == 0 || (bar & 0x1ULL))
                    continue;

                uint16_t command = pci_cfg_read16((uint8_t)bus, slot, func, 0x04);
                command |= 0x0006; // Memory Space + Bus Master
                pci_cfg_write16((uint8_t)bus, slot, func, 0x04, command);

                *out_bus = (uint8_t)bus;
                *out_slot = slot;
                *out_func = func;
                *out_vendor = vendor;
                *out_device = device;
                return bar;
            }
        }
    }

    return 0;
}

static bool wait_csts(uint32_t mask, bool set)
{
    for (uint32_t i = 0; i < DMA_TIMEOUT; ++i) {
        const uint32_t v = rd32(CSTS);
        if (((v & mask) != 0) == set)
            return true;
        __asm__ volatile("pause");
    }
    return false;
}

static uint32_t make_cdw0(uint8_t opcode, uint16_t cid)
{
    return uint32_t(opcode) | (uint32_t(cid) << 16);
}

static void clear_queue(void* p, size_t bytes)
{
    if (p)
        memset(p, 0, bytes);
}

static uint64_t page_masked(uint64_t a)
{
    return a & ~(uint64_t(PAGE_SIZE) - 1ULL);
}

static bool submit_admin(
    const NvmeCommand& cmd,
    NvmeCompletion* out)
{
    if (!g.admin_sq || !g.admin_cq)
        return false;

    const uint16_t cid = g.next_cid++;
    NvmeCommand* sq = reinterpret_cast<NvmeCommand*>(g.admin_sq);
    NvmeCompletion* cq = reinterpret_cast<NvmeCompletion*>(g.admin_cq);

    NvmeCommand c = cmd;
    c.cdw0 = (c.cdw0 & 0x0000FFFFu) | (uint32_t(cid) << 16);
    sq[g.admin_sq_tail] = c;
    __asm__ volatile("sfence" ::: "memory");

    g.admin_sq_tail = static_cast<uint16_t>((g.admin_sq_tail + 1) % QUEUE_DEPTH);
    wr32(DOORBELL_BASE + 0 * 2 * g.doorbell_stride, g.admin_sq_tail);

    for (uint32_t i = 0; i < DMA_TIMEOUT; ++i) {
        const NvmeCompletion cpl = cq[g.admin_cq_head];
        if (cpl.command_id == cid && (cpl.status & 0xFFFEu) == 0) {
            if (out)
                *out = cpl;
            g.admin_cq_head = static_cast<uint16_t>((g.admin_cq_head + 1) % QUEUE_DEPTH);
            wr32(DOORBELL_BASE + 1 * g.doorbell_stride, g.admin_cq_head);
            return true;
        }
        if (cpl.command_id == cid) {
            g.last_status = cpl.status;
            ++g.error_count;
            g.admin_cq_head = static_cast<uint16_t>((g.admin_cq_head + 1) % QUEUE_DEPTH);
            wr32(DOORBELL_BASE + 1 * g.doorbell_stride, g.admin_cq_head);
            return false;
        }
        __asm__ volatile("pause");
    }

    ++g.error_count;
    return false;
}

static bool admin_identify(uint8_t cns, uint32_t nsid, void* buffer)
{
    if (!buffer)
        return false;

    NvmeCommand cmd{};
    cmd.cdw0 = make_cdw0(ADMIN_IDENTIFY, 0);
    cmd.nsid = nsid;
    cmd.prp1 = reinterpret_cast<uint64_t>(buffer);
    cmd.cdw10 = uint32_t(cns);

    return submit_admin(cmd, nullptr);
}

static bool create_io_queues()
{
    if (!g.io_sq || !g.io_cq)
        return false;

    const uint64_t cq_addr = reinterpret_cast<uint64_t>(g.io_cq);
    const uint64_t sq_addr = reinterpret_cast<uint64_t>(g.io_sq);

    NvmeCommand cq{};
    cq.cdw0 = make_cdw0(ADMIN_CREATE_IO_CQ, 0);
    cq.prp1 = cq_addr;
    cq.cdw10 = (QUEUE_DEPTH - 1u) | (uint32_t(g.io_queue_id) << 16);
    cq.cdw11 = 0x1;
    if (!submit_admin(cq, nullptr))
        return false;

    NvmeCommand sq{};
    sq.cdw0 = make_cdw0(ADMIN_CREATE_IO_SQ, 0);
    sq.prp1 = sq_addr;
    sq.cdw10 = (QUEUE_DEPTH - 1u) | (uint32_t(g.io_queue_id) << 16);
    sq.cdw11 = 0x1 | (uint32_t(g.io_queue_id) << 16);
    return submit_admin(sq, nullptr);
}

static bool submit_io(
    uint8_t opcode,
    uint64_t lba,
    void* data)
{
    if (!g.ready || !data)
        return false;

    NvmeCommand* sq = reinterpret_cast<NvmeCommand*>(g.io_sq);
    NvmeCompletion* cq = reinterpret_cast<NvmeCompletion*>(g.io_cq);
    if (!sq || !cq)
        return false;

    const uint16_t cid = g.next_cid++;
    NvmeCommand cmd{};
    cmd.cdw0 = make_cdw0(opcode, cid);
    cmd.nsid = g.nsid;
    cmd.prp1 = reinterpret_cast<uint64_t>(data);
    cmd.cdw10 = uint32_t(lba & 0xFFFFFFFFULL);
    cmd.cdw11 = uint32_t((lba >> 32) & 0xFFFFFFFFULL);
    cmd.cdw12 = 0; // one logical block, zero-based count

    sq[g.io_sq_tail] = cmd;
    __asm__ volatile("sfence" ::: "memory");
    g.io_sq_tail = static_cast<uint16_t>((g.io_sq_tail + 1) % QUEUE_DEPTH);
    wr32(DOORBELL_BASE + (2 * g.io_queue_id) * g.doorbell_stride, g.io_sq_tail);

    for (uint32_t i = 0; i < DMA_TIMEOUT; ++i) {
        const NvmeCompletion cpl = cq[g.io_cq_head];
        if (cpl.command_id == cid) {
            g.last_status = cpl.status;
            g.io_cq_head = static_cast<uint16_t>((g.io_cq_head + 1) % QUEUE_DEPTH);
            wr32(DOORBELL_BASE + (2 * g.io_queue_id + 1) * g.doorbell_stride, g.io_cq_head);
            if (cpl.status & 0xFFFEu) {
                ++g.error_count;
                return false;
            }
            return true;
        }
        __asm__ volatile("pause");
    }

    ++g.error_count;
    return false;
}

static bool discover_namespace()
{
    uint8_t* ctrl = reinterpret_cast<uint8_t*>(dma::alloc(PAGE_SIZE, PAGE_SIZE));
    uint8_t* ns = reinterpret_cast<uint8_t*>(dma::alloc(PAGE_SIZE, PAGE_SIZE));
    if (!ctrl || !ns)
        return false;

    clear_queue(ctrl, PAGE_SIZE);
    clear_queue(ns, PAGE_SIZE);

    if (!admin_identify(IDENTIFY_CONTROLLER, 0, ctrl) ||
        !admin_identify(IDENTIFY_NAMESPACE, g.nsid, ns)) {
        dma::free(ctrl);
        dma::free(ns);
        return false;
    }

    memcpy(g.model, ctrl + 24, 40);
    g.model[40] = '\0';
    for (int i = 39; i >= 0; --i) {
        if (g.model[i] == ' ' || g.model[i] == '\0')
            g.model[i] = '\0';
        else
            break;
    }

    uint64_t nsze = 0;
    memcpy(&nsze, ns + 0, sizeof(nsze));
    if (nsze == 0) {
        dma::free(ctrl);
        dma::free(ns);
        return false;
    }

    const uint8_t flbas = ns[26] & 0x0F;
    uint8_t lbads = 9;
    const size_t lf = 128 + static_cast<size_t>(flbas) * 4;
    if (lf + 3 < PAGE_SIZE) {
        uint32_t lba_fmt = 0;
        memcpy(&lba_fmt, ns + lf, sizeof(lba_fmt));
        const uint8_t candidate = static_cast<uint8_t>((lba_fmt >> 16) & 0xFF);
        if (candidate >= 9 && candidate <= 16)
            lbads = candidate;
    }

    g.sector_bytes = 1u << lbads;
    if (g.sector_bytes != 512) {
        /* BlockOS currently exposes 512-byte logical sectors. Refuse a
           namespace whose native logical block size cannot be represented
           safely instead of silently corrupting LBA calculations. */
        dma::free(ctrl);
        dma::free(ns);
        return false;
    }
    g.namespace_sectors = nsze;

    dma::free(ctrl);
    dma::free(ns);
    return true;
}

static bool register_backend()
{
    if (g.registered)
        return true;

    if (!blockos_storage_register_backend(
            "nvme0",
            [](uint64_t lba, uint32_t count, void* out) -> bool {
                return read_sectors(lba, count, out);
            },
            [](uint64_t lba, uint32_t count, const void* in) -> bool {
                return write_sectors(lba, count, in);
            },
            []() -> bool { return flush(); },
            nullptr,
            g.namespace_sectors,
            512,
            0,
            false,
            false,
            true))
        return false;

    g.registered = true;
    hardware_center.register_pci_device(
        "nvme0",
        DEV_TYPE_STORAGE,
        g.bus,
        g.slot,
        g.func,
        g.vendor,
        g.device,
        PCI_CLASS_STORAGE,
        PCI_SUBCLASS_NVM,
        PCI_PROGIF_NVM,
        g.bar0,
        0,
        0);
    return true;
}

} // namespace

bool init()
{
    if (g.ready)
        return true;

    if (g.bar0 == 0) {
        g.bar0 = pci_find_bar0(&g.bus, &g.slot, &g.func, &g.vendor, &g.device);
        if (g.bar0 == 0)
            return false;
    }

    g.cap = rd64(CAP);
    g.version = rd32(VS);
    const uint8_t dstrd = static_cast<uint8_t>((g.cap >> 32) & 0x0F);
    g.doorbell_stride = 4u << dstrd;

    const uint32_t c = rd32(CC);
    if (c & CC_EN) {
        wr32(CC, c & ~CC_EN);
        if (!wait_csts(CSTS_RDY, false))
            return false;
    }

    const uint32_t timeout_ms_units = static_cast<uint32_t>((g.cap >> 24) & 0xFF);
    (void)timeout_ms_units;

    g.admin_sq = dma::alloc(sizeof(NvmeCommand) * QUEUE_DEPTH, PAGE_SIZE);
    g.admin_cq = dma::alloc(sizeof(NvmeCompletion) * QUEUE_DEPTH, PAGE_SIZE);
    g.io_sq = dma::alloc(sizeof(NvmeCommand) * QUEUE_DEPTH, PAGE_SIZE);
    g.io_cq = dma::alloc(sizeof(NvmeCompletion) * QUEUE_DEPTH, PAGE_SIZE);
    g.scratch = dma::alloc(PAGE_SIZE, PAGE_SIZE);
    if (!g.admin_sq || !g.admin_cq || !g.io_sq || !g.io_cq || !g.scratch)
        return false;

    clear_queue(g.admin_sq, sizeof(NvmeCommand) * QUEUE_DEPTH);
    clear_queue(g.admin_cq, sizeof(NvmeCompletion) * QUEUE_DEPTH);
    clear_queue(g.io_sq, sizeof(NvmeCommand) * QUEUE_DEPTH);
    clear_queue(g.io_cq, sizeof(NvmeCompletion) * QUEUE_DEPTH);
    clear_queue(g.scratch, PAGE_SIZE);

    wr32(AQA, ((QUEUE_DEPTH - 1u) << 16) | (QUEUE_DEPTH - 1u));
    wr64(ASQ, reinterpret_cast<uint64_t>(g.admin_sq));
    wr64(ACQ, reinterpret_cast<uint64_t>(g.admin_cq));

    wr32(CC, (6u << 16) | (4u << 20)); // IOSQES=64B, IOCQES=16B, MPS=0
    wr32(CC, rd32(CC) | CC_EN);
    g.ready = wait_csts(CSTS_RDY, true);
    if (!g.ready)
        return false;

    if (rd32(CSTS) & CSTS_CFS) {
        g.ready = false;
        ++g.error_count;
        return false;
    }

    if (!discover_namespace()) {
        g.ready = false;
        return false;
    }

    if (!create_io_queues()) {
        g.ready = false;
        return false;
    }

    if (!register_backend()) {
        g.ready = false;
        return false;
    }

    Print((CHAR16*)L"nvme0: controller online\n");
    return true;
}

bool is_ready() { return g.ready; }
uint64_t capacity_sectors() { return g.namespace_sectors; }
uint32_t sector_size() { return 512; }
uint32_t controller_status() { return g.last_status; }
uint32_t controller_error_count() { return g.error_count; }

bool read_sector(uint64_t lba, void* buffer)
{
    if (!g.ready || !buffer || lba >= g.namespace_sectors)
        return false;
    if (!submit_io(IO_READ, lba, g.scratch))
        return false;
    memcpy(buffer, g.scratch, 512);
    return true;
}

bool write_sector(uint64_t lba, const void* buffer)
{
    if (!g.ready || !buffer || lba >= g.namespace_sectors)
        return false;
    memcpy(g.scratch, buffer, 512);
    return submit_io(IO_WRITE, lba, g.scratch);
}

bool read_sectors(uint64_t lba, uint32_t count, void* buffer)
{
    if (!buffer || count == 0)
        return false;
    if (lba >= g.namespace_sectors || count > g.namespace_sectors - lba)
        return false;

    uint8_t* p = static_cast<uint8_t*>(buffer);
    for (uint32_t i = 0; i < count; ++i) {
        if (!read_sector(lba + i, p + i * 512u))
            return false;
    }
    return true;
}

bool write_sectors(uint64_t lba, uint32_t count, const void* buffer)
{
    if (!buffer || count == 0)
        return false;
    if (lba >= g.namespace_sectors || count > g.namespace_sectors - lba)
        return false;

    const uint8_t* p = static_cast<const uint8_t*>(buffer);
    for (uint32_t i = 0; i < count; ++i) {
        if (!write_sector(lba + i, p + i * 512u))
            return false;
    }
    return true;
}

bool flush()
{
    if (!g.ready)
        return false;
    NvmeCommand cmd{};
    cmd.cdw0 = make_cdw0(IO_FLUSH, g.next_cid++);
    cmd.nsid = g.nsid;
    /* Flush is submitted through the normal I/O queue path without data. */
    NvmeCommand* sq = reinterpret_cast<NvmeCommand*>(g.io_sq);
    NvmeCompletion* cq = reinterpret_cast<NvmeCompletion*>(g.io_cq);
    if (!sq || !cq)
        return false;
    const uint16_t cid = static_cast<uint16_t>(cmd.cdw0 >> 16);
    cmd.cdw0 = make_cdw0(IO_FLUSH, cid);
    sq[g.io_sq_tail] = cmd;
    __asm__ volatile("sfence" ::: "memory");
    g.io_sq_tail = static_cast<uint16_t>((g.io_sq_tail + 1) % QUEUE_DEPTH);
    wr32(DOORBELL_BASE + (2 * g.io_queue_id) * g.doorbell_stride, g.io_sq_tail);
    for (uint32_t i = 0; i < DMA_TIMEOUT; ++i) {
        NvmeCompletion cpl = cq[g.io_cq_head];
        if (cpl.command_id == cid) {
            g.last_status = cpl.status;
            g.io_cq_head = static_cast<uint16_t>((g.io_cq_head + 1) % QUEUE_DEPTH);
            wr32(DOORBELL_BASE + (2 * g.io_queue_id + 1) * g.doorbell_stride, g.io_cq_head);
            if (cpl.status & 0xFFFEu) {
                ++g.error_count;
                return false;
            }
            return true;
        }
        __asm__ volatile("pause");
    }
    ++g.error_count;
    return false;
}

} // namespace nvme
