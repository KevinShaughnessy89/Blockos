#include "rtl8139.hpp"
#include "pci.hpp"
#include "dma.hpp"
#include "device_manager.hpp"

#include <string.h>

namespace rtl8139 {
namespace {

constexpr uint16_t REALTEK_VENDOR = 0x10EC;
constexpr size_t RX_SIZE = 65536 + 16;
constexpr size_t TX_COUNT = 4;
constexpr size_t TX_SIZE = 2048;

constexpr uint8_t REG_IDR0 = 0x00;
constexpr uint8_t REG_TSD0 = 0x10;
constexpr uint8_t REG_TSAD0 = 0x20;
constexpr uint8_t REG_RBSTART = 0x30;
constexpr uint8_t REG_CMD = 0x37;
constexpr uint8_t REG_CAPR = 0x38;
constexpr uint8_t REG_CBR = 0x3A;
constexpr uint8_t REG_IMR = 0x3C;
constexpr uint8_t REG_ISR = 0x3E;
constexpr uint8_t REG_TCR = 0x40;
constexpr uint8_t REG_RCR = 0x44;
constexpr uint8_t REG_CONFIG1 = 0x52;

constexpr uint8_t CMD_RESET = 0x10;
constexpr uint8_t CMD_RX_ENABLE = 0x08;
constexpr uint8_t CMD_TX_ENABLE = 0x04;

constexpr uint16_t TSD_TOK = 0x8000;
constexpr uint16_t TSD_TABT = 0x4000;
constexpr uint16_t TSD_CRS = 0x2000;
constexpr uint16_t TSD_OWC = 0x1000;

constexpr uint16_t RX_STATUS_ROK = 0x0001;
constexpr uint32_t RCR_AAP = 1u << 0;
constexpr uint32_t RCR_APM = 1u << 1;
constexpr uint32_t RCR_AM = 1u << 2;
constexpr uint32_t RCR_AB = 1u << 3;
constexpr uint32_t RCR_WRAP = 1u << 7;
constexpr uint32_t RCR_MXDMA_1024 = 7u << 8;
constexpr uint32_t RCR_RBLEN_64K = 3u << 11;

struct State {
    bool present = false;
    bool initialized = false;
    bool io = false;
    uint8_t bus = 0;
    uint8_t slot = 0;
    uint8_t func = 0;
    uint16_t vendor = 0;
    uint16_t device = 0;
    uint16_t io_base = 0;
    uint64_t mmio = 0;
    uint8_t* rx = nullptr;
    uint8_t* tx[TX_COUNT]{};
    uint8_t mac[6]{};
    uint32_t rx_offset = 0;
    uint32_t tx_index = 0;
};

State g{};

static inline void out8(uint16_t p, uint8_t v)
{
    __asm__ volatile("outb %0, %1" : : "a"(v), "dN"(p));
}
static inline void out16(uint16_t p, uint16_t v)
{
    __asm__ volatile("outw %0, %1" : : "a"(v), "dN"(p));
}
static inline void out32(uint16_t p, uint32_t v)
{
    __asm__ volatile("outl %0, %1" : : "a"(v), "dN"(p));
}
static inline uint8_t in8(uint16_t p)
{
    uint8_t v;
    __asm__ volatile("inb %1, %0" : "=a"(v) : "dN"(p));
    return v;
}
static inline uint16_t in16(uint16_t p)
{
    uint16_t v;
    __asm__ volatile("inw %1, %0" : "=a"(v) : "dN"(p));
    return v;
}
static inline uint32_t in32(uint16_t p)
{
    uint32_t v;
    __asm__ volatile("inl %1, %0" : "=a"(v) : "dN"(p));
    return v;
}

static inline volatile uint8_t* mmio8(uint64_t off)
{
    return reinterpret_cast<volatile uint8_t*>(
        static_cast<uintptr_t>(g.mmio + off));
}
static inline uint8_t read_reg8(uint8_t off)
{
    return g.io ? in8(static_cast<uint16_t>(g.io_base + off)) : *mmio8(off);
}
static inline void write_reg8(uint8_t off, uint8_t v)
{
    if (g.io) out8(static_cast<uint16_t>(g.io_base + off), v);
    else *mmio8(off) = v;
}
static inline uint16_t read_reg16(uint8_t off)
{
    if (g.io) return in16(static_cast<uint16_t>(g.io_base + off));
    return *reinterpret_cast<volatile uint16_t*>(
        static_cast<uintptr_t>(g.mmio + off));
}
static inline void write_reg16(uint8_t off, uint16_t v)
{
    if (g.io) out16(static_cast<uint16_t>(g.io_base + off), v);
    else *reinterpret_cast<volatile uint16_t*>(
        static_cast<uintptr_t>(g.mmio + off)) = v;
}
static inline uint32_t read_reg32(uint8_t off)
{
    if (g.io) return in32(static_cast<uint16_t>(g.io_base + off));
    return *reinterpret_cast<volatile uint32_t*>(
        static_cast<uintptr_t>(g.mmio + off));
}
static inline void write_reg32(uint8_t off, uint32_t v)
{
    if (g.io) out32(static_cast<uint16_t>(g.io_base + off), v);
    else *reinterpret_cast<volatile uint32_t*>(
        static_cast<uintptr_t>(g.mmio + off)) = v;
}

static bool valid_mac()
{
    bool all_zero = true;
    bool all_ff = true;
    for (uint8_t b : g.mac) {
        if (b != 0) all_zero = false;
        if (b != 0xFF) all_ff = false;
    }
    return !all_zero && !all_ff && ((g.mac[0] & 1u) == 0);
}

static void read_mac()
{
    for (size_t i = 0; i < 6; ++i)
        g.mac[i] = read_reg8(static_cast<uint8_t>(REG_IDR0 + i));
}

} // namespace

bool probe(uint8_t bus, uint8_t slot, uint8_t func)
{
    if (pci_cfg_read16(bus, slot, func, 0x00) != REALTEK_VENDOR)
        return false;

    switch (pci_cfg_read16(bus, slot, func, 0x02)) {
        case 0x8139:
        case 0x8138:
        case 0x8100:
            return true;
        default:
            return false;
    }
}

bool init(uint8_t bus, uint8_t slot, uint8_t func)
{
    if (!probe(bus, slot, func))
        return false;

    g = {};
    g.present = true;
    g.bus = bus;
    g.slot = slot;
    g.func = func;
    g.vendor = pci_cfg_read16(bus, slot, func, 0x00);
    g.device = pci_cfg_read16(bus, slot, func, 0x02);

    const uint32_t bar0 = pci_cfg_read32(bus, slot, func, 0x10);
    g.io = (bar0 & 1u) != 0;
    if (g.io)
        g.io_base = static_cast<uint16_t>(bar0 & ~3u);
    else
        g.mmio = static_cast<uint64_t>(bar0 & ~0xFu);

    if (!g.io && !g.mmio)
        return false;

    uint16_t command = pci_cfg_read16(bus, slot, func, 0x04);
    command |= 0x0005;
    pci_cfg_write16(bus, slot, func, 0x04, command);

    write_reg8(REG_CONFIG1, 0x00);
    write_reg8(REG_CMD, CMD_RESET);

    for (uint32_t i = 0; i < 1000000; ++i) {
        if ((read_reg8(REG_CMD) & CMD_RESET) == 0)
            break;
        __asm__ volatile("pause");
    }

    g.rx = static_cast<uint8_t*>(dma::alloc(RX_SIZE, 256));
    if (!g.rx)
        return false;
    memset(g.rx, 0, RX_SIZE);

    for (size_t i = 0; i < TX_COUNT; ++i) {
        g.tx[i] = static_cast<uint8_t*>(dma::alloc(TX_SIZE, 16));
        if (!g.tx[i])
            return false;
        memset(g.tx[i], 0, TX_SIZE);
        write_reg32(static_cast<uint8_t>(REG_TSAD0 + i * 4),
                    static_cast<uint32_t>(reinterpret_cast<uint64_t>(g.tx[i])));
    }

    write_reg32(REG_RBSTART, static_cast<uint32_t>(reinterpret_cast<uint64_t>(g.rx)));
    write_reg32(REG_RCR,
                RCR_AAP | RCR_APM | RCR_AM |
                RCR_AB | RCR_WRAP |
                RCR_MXDMA_1024 | RCR_RBLEN_64K);
    write_reg32(REG_TCR, 0);

    write_reg8(REG_IMR, 0);
    write_reg8(static_cast<uint8_t>(REG_IMR + 1), 0);

    write_reg8(REG_CMD, CMD_RX_ENABLE | CMD_TX_ENABLE);

    read_mac();
    if (!valid_mac())
        return false;

    g.rx_offset = 0;
    g.tx_index = 0;
    g.initialized = true;

    hardware_center.register_pci_device(
        "rtl8139",
        DEV_TYPE_NETWORK,
        bus, slot, func,
        g.vendor, g.device,
        0x02, 0x00, 0x00,
        g.mmio,
        0,
        pci_cfg_read8(bus, slot, func, 0x3C)
    );

    return true;
}

bool ready()
{
    return g.initialized;
}

bool send(const uint8_t* data, size_t length)
{
    if (!g.initialized || !data || length == 0 || length > TX_SIZE)
        return false;

    const size_t i = g.tx_index;
    memcpy(g.tx[i], data, length);

    const uint8_t tsd = static_cast<uint8_t>(REG_TSD0 + i * 4);
    write_reg32(static_cast<uint8_t>(REG_TSAD0 + i * 4),
                static_cast<uint32_t>(reinterpret_cast<uint64_t>(g.tx[i])));
    write_reg32(tsd, static_cast<uint32_t>(length));

    for (uint32_t n = 0; n < 200000; ++n) {
        const uint32_t status = read_reg32(tsd);
        if (status & TSD_TOK)
            break;
        if (status & (TSD_TABT | TSD_CRS | TSD_OWC))
            return false;
        __asm__ volatile("pause");
    }

    g.tx_index = static_cast<uint32_t>((g.tx_index + 1) % TX_COUNT);
    return true;
}

bool receive(uint8_t* buffer, size_t capacity, size_t* out_length)
{
    if (!g.initialized || !buffer || !out_length || capacity == 0)
        return false;

    const uint16_t capr = read_reg16(REG_CAPR);
    const uint16_t cbr = read_reg16(REG_CBR);
    if (capr == cbr)
        return false;

    uint32_t offset = g.rx_offset;
    if (offset + 4 >= RX_SIZE)
        offset = 0;

    const uint16_t status =
        static_cast<uint16_t>(g.rx[offset] |
                              (static_cast<uint16_t>(g.rx[offset + 1]) << 8));
    const uint16_t length =
        static_cast<uint16_t>(g.rx[offset + 2] |
                              (static_cast<uint16_t>(g.rx[offset + 3]) << 8));

    if ((status & RX_STATUS_ROK) == 0 || length < 4 || length > 16384) {
        g.rx_offset = 0;
        write_reg16(REG_CAPR, 0);
        return false;
    }

    size_t packet_len = static_cast<size_t>(length) - 4;
    if (packet_len > capacity)
        packet_len = capacity;

    uint32_t packet_offset = offset + 4;
    if (packet_offset + packet_len <= RX_SIZE) {
        memcpy(buffer, g.rx + packet_offset, packet_len);
    } else {
        const size_t first = RX_SIZE - packet_offset;
        memcpy(buffer, g.rx + packet_offset, first);
        memcpy(buffer + first, g.rx, packet_len - first);
    }

    *out_length = packet_len;

    g.rx_offset = (offset + length + 3) & ~3u;
    if (g.rx_offset >= RX_SIZE)
        g.rx_offset = 0;

    const uint16_t new_capr =
        static_cast<uint16_t>((g.rx_offset + RX_SIZE - 0x10) & 0xFFFFu);
    write_reg16(REG_CAPR, new_capr);

    return true;
}

void get_mac(uint8_t out[6])
{
    if (!out) return;
    memcpy(out, g.mac, sizeof(g.mac));
}

uint16_t vendor_id() { return g.vendor; }
uint16_t device_id() { return g.device; }

} // namespace rtl8139
