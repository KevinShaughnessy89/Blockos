#include "intel_e1000.hpp"
#include "pci.hpp"
#include "dma.hpp"
#include "device_manager.hpp"

#include <string.h>

namespace intel_e1000 {
namespace {

constexpr uint16_t INTEL_VENDOR = 0x8086;

constexpr uint32_t REG_CTRL  = 0x0000;
constexpr uint32_t REG_STATUS = 0x0008;
constexpr uint32_t REG_EERD = 0x0014;
constexpr uint32_t REG_ICR = 0x00C0;
constexpr uint32_t REG_IMS = 0x00D0;
constexpr uint32_t REG_RCTL = 0x0100;
constexpr uint32_t REG_TCTL = 0x0400;
constexpr uint32_t REG_TIPG = 0x0410;
constexpr uint32_t REG_RDBAL = 0x2800;
constexpr uint32_t REG_RDBAH = 0x2804;
constexpr uint32_t REG_RDLEN = 0x2808;
constexpr uint32_t REG_RDH = 0x2810;
constexpr uint32_t REG_RDT = 0x2818;
constexpr uint32_t REG_TDBAL = 0x3800;
constexpr uint32_t REG_TDBAH = 0x3804;
constexpr uint32_t REG_TDLEN = 0x3808;
constexpr uint32_t REG_TDH = 0x3810;
constexpr uint32_t REG_TDT = 0x3818;
constexpr uint32_t REG_RAL0 = 0x5400;
constexpr uint32_t REG_RAH0 = 0x5404;

constexpr uint32_t CTRL_RST = 0x04000000u;
constexpr uint32_t CTRL_SLU = 0x00000040u;
constexpr uint32_t STATUS_LU = 0x00000002u;

constexpr uint32_t RCTL_EN = 0x00000002u;
constexpr uint32_t RCTL_BAM = 0x00000004u;
constexpr uint32_t RCTL_BSE = 0x00000020u;
constexpr uint32_t RCTL_SECRC = 0x04000000u;
constexpr uint32_t RCTL_LPE = 0x00000020u;
constexpr uint32_t RCTL_SZ_2048 = 0x00000000u;

constexpr uint32_t TCTL_EN = 0x00000002u;
constexpr uint32_t TCTL_PSP = 0x00000008u;
constexpr uint32_t TCTL_CT = 0x00000FF0u;
constexpr uint32_t TCTL_COLD = 0x003FF000u;

constexpr uint8_t TX_CMD_EOP = 1u << 0;
constexpr uint8_t TX_CMD_IFCS = 1u << 1;
constexpr uint8_t TX_CMD_RS = 1u << 3;
constexpr uint8_t TX_STATUS_DD = 1u << 0;
constexpr uint8_t RX_STATUS_DD = 1u << 0;
constexpr uint8_t RX_STATUS_EOP = 1u << 1;

constexpr size_t RX_COUNT = 32;
constexpr size_t TX_COUNT = 32;
constexpr size_t BUF_SIZE = 2048;

struct RxDesc {
    uint64_t addr;
    uint16_t length;
    uint16_t checksum;
    uint8_t status;
    uint8_t errors;
    uint16_t special;
} __attribute__((packed, aligned(16)));

struct TxDesc {
    uint64_t addr;
    uint16_t length;
    uint8_t cso;
    uint8_t cmd;
    uint8_t status;
    uint8_t css;
    uint16_t special;
} __attribute__((packed, aligned(16)));

struct State {
    bool present = false;
    bool initialized = false;
    uint8_t bus = 0;
    uint8_t slot = 0;
    uint8_t func = 0;
    uint16_t vendor = 0;
    uint16_t device = 0;
    uint64_t mmio = 0;

    RxDesc* rx_desc = nullptr;
    TxDesc* tx_desc = nullptr;
    uint8_t* rx_buf[RX_COUNT]{};
    uint8_t* tx_buf[TX_COUNT]{};
    uint16_t rx_consume = 0;
    uint16_t tx_next = 0;
    uint8_t mac[6]{};
};

State g{};

static inline volatile uint32_t* reg(uint32_t offset)
{
    return reinterpret_cast<volatile uint32_t*>(
        static_cast<uintptr_t>(g.mmio + offset));
}

static inline uint32_t read32(uint32_t offset)
{
    return *reg(offset);
}

static inline void write32(uint32_t offset, uint32_t value)
{
    *reg(offset) = value;
}

static void wait_reset()
{
    for (uint32_t i = 0; i < 1000000; ++i) {
        if ((read32(REG_CTRL) & CTRL_RST) == 0)
            return;
        __asm__ volatile("pause");
    }
}

static bool read_eeprom_word(uint16_t address, uint16_t* out)
{
    if (!out)
        return false;

    const uint32_t cmd =
        (1u << 0) |
        (static_cast<uint32_t>(address) << 8);

    write32(REG_EERD, cmd);

    for (uint32_t i = 0; i < 200000; ++i) {
        const uint32_t v = read32(REG_EERD);
        if (v & (1u << 4)) {
            *out = static_cast<uint16_t>(v >> 16);
            return true;
        }
        __asm__ volatile("pause");
    }

    return false;
}

static void read_mac()
{
    const uint32_t low = read32(REG_RAL0);
    const uint32_t high = read32(REG_RAH0);

    g.mac[0] = static_cast<uint8_t>(low & 0xFF);
    g.mac[1] = static_cast<uint8_t>((low >> 8) & 0xFF);
    g.mac[2] = static_cast<uint8_t>((low >> 16) & 0xFF);
    g.mac[3] = static_cast<uint8_t>((low >> 24) & 0xFF);
    g.mac[4] = static_cast<uint8_t>(high & 0xFF);
    g.mac[5] = static_cast<uint8_t>((high >> 8) & 0xFF);

    bool bad = true;
    for (uint8_t b : g.mac) {
        if (b != 0 && b != 0xFF) {
            bad = false;
            break;
        }
    }

    if (bad) {
        uint16_t w0 = 0, w1 = 0, w2 = 0;
        if (read_eeprom_word(0, &w0) &&
            read_eeprom_word(1, &w1) &&
            read_eeprom_word(2, &w2)) {
            g.mac[0] = static_cast<uint8_t>(w0 & 0xFF);
            g.mac[1] = static_cast<uint8_t>(w0 >> 8);
            g.mac[2] = static_cast<uint8_t>(w1 & 0xFF);
            g.mac[3] = static_cast<uint8_t>(w1 >> 8);
            g.mac[4] = static_cast<uint8_t>(w2 & 0xFF);
            g.mac[5] = static_cast<uint8_t>(w2 >> 8);
        }
    }
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

static bool allocate_rings()
{
    g.rx_desc = static_cast<RxDesc*>(
        dma::alloc(sizeof(RxDesc) * RX_COUNT, 16));
    g.tx_desc = static_cast<TxDesc*>(
        dma::alloc(sizeof(TxDesc) * TX_COUNT, 16));

    if (!g.rx_desc || !g.tx_desc)
        return false;

    memset(g.rx_desc, 0, sizeof(RxDesc) * RX_COUNT);
    memset(g.tx_desc, 0, sizeof(TxDesc) * TX_COUNT);

    for (size_t i = 0; i < RX_COUNT; ++i) {
        g.rx_buf[i] = static_cast<uint8_t*>(dma::alloc(BUF_SIZE, 16));
        if (!g.rx_buf[i])
            return false;
        memset(g.rx_buf[i], 0, BUF_SIZE);
        g.rx_desc[i].addr = reinterpret_cast<uint64_t>(g.rx_buf[i]);
        g.rx_desc[i].status = 0;
    }

    for (size_t i = 0; i < TX_COUNT; ++i) {
        g.tx_buf[i] = static_cast<uint8_t*>(dma::alloc(BUF_SIZE, 16));
        if (!g.tx_buf[i])
            return false;
        memset(g.tx_buf[i], 0, BUF_SIZE);
        g.tx_desc[i].addr = reinterpret_cast<uint64_t>(g.tx_buf[i]);
        g.tx_desc[i].status = TX_STATUS_DD;
    }

    return true;
}

static void setup_rings()
{
    const uint64_t rx = reinterpret_cast<uint64_t>(g.rx_desc);
    write32(REG_RDBAL, static_cast<uint32_t>(rx));
    write32(REG_RDBAH, static_cast<uint32_t>(rx >> 32));
    write32(REG_RDLEN, static_cast<uint32_t>(sizeof(RxDesc) * RX_COUNT));
    write32(REG_RDH, 0);
    write32(REG_RDT, RX_COUNT - 1);

    const uint64_t tx = reinterpret_cast<uint64_t>(g.tx_desc);
    write32(REG_TDBAL, static_cast<uint32_t>(tx));
    write32(REG_TDBAH, static_cast<uint32_t>(tx >> 32));
    write32(REG_TDLEN, static_cast<uint32_t>(sizeof(TxDesc) * TX_COUNT));
    write32(REG_TDH, 0);
    write32(REG_TDT, 0);
}

} // namespace

bool probe(uint8_t bus, uint8_t slot, uint8_t func)
{
    const uint16_t vendor = pci_cfg_read16(bus, slot, func, 0x00);
    if (vendor != INTEL_VENDOR)
        return false;

    const uint16_t device = pci_cfg_read16(bus, slot, func, 0x02);

    switch (device) {
        case 0x100E: case 0x100F: case 0x1004: case 0x1005:
        case 0x100C: case 0x1015: case 0x1016: case 0x1017:
        case 0x101E: case 0x1026: case 0x1049: case 0x104A:
        case 0x104B: case 0x104D: case 0x105E: case 0x1075:
        case 0x1076: case 0x1077: case 0x1078: case 0x1079:
        case 0x107A: case 0x107B: case 0x107C: case 0x107D:
        case 0x107E: case 0x107F: case 0x108A: case 0x108B:
        case 0x10D3: case 0x10EA:
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
    g.mmio = pci_read_bar(bus, slot, func, 0);

    if (!g.mmio)
        return false;

    uint16_t command = pci_cfg_read16(bus, slot, func, 0x04);
    command |= 0x0006;
    pci_cfg_write16(bus, slot, func, 0x04, command);

    write32(REG_CTRL, CTRL_RST);
    wait_reset();

    write32(REG_CTRL, CTRL_SLU);
    write32(REG_IMS, 0);
    (void)read32(REG_ICR);

    if (!allocate_rings())
        return false;

    setup_rings();

    write32(REG_RCTL,
             RCTL_EN |
             RCTL_BAM |
             RCTL_SECRC |
             RCTL_SZ_2048);

    write32(REG_TCTL,
             TCTL_EN |
             TCTL_PSP |
             TCTL_CT |
             TCTL_COLD);

    write32(REG_TIPG, 0x0060200A);

    read_mac();
    if (!valid_mac())
        return false;

    g.rx_consume = 0;
    g.tx_next = 0;
    g.initialized = true;

    hardware_center.register_pci_device(
        "e1000",
        DEV_TYPE_NETWORK,
        bus, slot, func,
        g.vendor, g.device,
        0x02, 0x00, 0x00,
        g.mmio, 0,
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
    if (!g.initialized || !data || length == 0 || length > BUF_SIZE)
        return false;

    TxDesc& d = g.tx_desc[g.tx_next];

    if ((d.status & TX_STATUS_DD) == 0)
        return false;

    memcpy(g.tx_buf[g.tx_next], data, length);
    d.addr = reinterpret_cast<uint64_t>(g.tx_buf[g.tx_next]);
    d.length = static_cast<uint16_t>(length);
    d.cso = 0;
    d.cmd = TX_CMD_EOP | TX_CMD_IFCS | TX_CMD_RS;
    d.status = 0;
    d.css = 0;
    d.special = 0;

    __asm__ volatile("sfence" ::: "memory");

    const uint16_t idx = g.tx_next;
    g.tx_next = static_cast<uint16_t>((g.tx_next + 1) % TX_COUNT);
    write32(REG_TDT, g.tx_next);

    for (uint32_t i = 0; i < 100000; ++i) {
        if (g.tx_desc[idx].status & TX_STATUS_DD)
            return true;
        __asm__ volatile("pause");
    }

    return false;
}

bool receive(uint8_t* buffer, size_t capacity, size_t* out_length)
{
    if (!g.initialized || !buffer || !out_length || capacity == 0)
        return false;

    RxDesc& d = g.rx_desc[g.rx_consume];

    if ((d.status & RX_STATUS_DD) == 0)
        return false;

    size_t n = d.length;
    if (n > capacity)
        n = capacity;

    memcpy(buffer, g.rx_buf[g.rx_consume], n);
    *out_length = n;

    d.status = 0;
    __asm__ volatile("sfence" ::: "memory");

    const uint16_t old = g.rx_consume;
    g.rx_consume = static_cast<uint16_t>((g.rx_consume + 1) % RX_COUNT);
    write32(REG_RDT, old);

    return true;
}

void get_mac(uint8_t out[6])
{
    if (!out) return;
    memcpy(out, g.mac, sizeof(g.mac));
}

uint16_t vendor_id() { return g.vendor; }
uint16_t device_id() { return g.device; }

} // namespace intel_e1000
