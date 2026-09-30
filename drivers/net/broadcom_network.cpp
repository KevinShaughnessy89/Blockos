#include "broadcom_network.hpp"
#include "pci.hpp"
#include "device_manager.hpp"

namespace broadcom_network {
namespace {

constexpr uint16_t BROADCOM_VENDOR = 0x14E4;

struct State {
    bool present = false;
    bool initialized = false;
    uint8_t bus = 0;
    uint8_t slot = 0;
    uint8_t func = 0;
    uint16_t vendor = 0;
    uint16_t device = 0;
    uint64_t bar0 = 0;
    uint8_t mac[6]{};
};

State g{};

static bool supported_device(uint16_t id)
{
    switch (id) {
        case 0x1644: case 0x1645: case 0x1646: case 0x1647:
        case 0x1648: case 0x1649: case 0x164A: case 0x164B:
        case 0x1653: case 0x1654: case 0x1657: case 0x165D:
        case 0x165E: case 0x165F: case 0x1668: case 0x1669:
        case 0x1677: case 0x1678: case 0x1679: case 0x167A:
        case 0x1680: case 0x1681: case 0x1682: case 0x1684:
        case 0x1686: case 0x1687: case 0x1690: case 0x1691:
        case 0x1692: case 0x1693: case 0x1694: case 0x16A6:
        case 0x16A7: case 0x16A8: case 0x16A9: case 0x16AA:
        case 0x16B0: case 0x16B1: case 0x16B2: case 0x16B4:
        case 0x16B5: case 0x16B6: case 0x16B7: case 0x16B9:
        case 0x16BC: case 0x16BD: case 0x16BE: case 0x16BF:
            return true;
        default:
            return false;
    }
}

static void enable_bus_mastering()
{
    uint16_t command =
        pci_cfg_read16(g.bus, g.slot, g.func, 0x04);
    command |= 0x0006;
    pci_cfg_write16(g.bus, g.slot, g.func, 0x04, command);
}

static void try_read_mac_from_bar()
{
    /*
     * Broadcom Tigon3-family MAC storage and DMA initialization are
     * controller/revision-specific. Do not guess a universal MAC offset.
     * Leave the address zeroed unless a future tg3-specific backend supplies it.
     */
    for (uint8_t& b : g.mac)
        b = 0;
}

} // namespace

bool probe(uint8_t bus, uint8_t slot, uint8_t func)
{
    if (pci_cfg_read16(bus, slot, func, 0x00) != BROADCOM_VENDOR)
        return false;

    const uint8_t class_id =
        pci_cfg_read8(bus, slot, func, 0x0B);
    const uint8_t subclass =
        pci_cfg_read8(bus, slot, func, 0x0A);

    if (class_id != 0x02 || subclass != 0x00)
        return false;

    return supported_device(
        pci_cfg_read16(bus, slot, func, 0x02)
    );
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
    g.bar0 = pci_read_bar(bus, slot, func, 0);

    if (!g.bar0)
        return false;

    enable_bus_mastering();
    try_read_mac_from_bar();

    g.initialized = true;

    hardware_center.register_pci_device(
        "broadcom-tg3",
        DEV_TYPE_NETWORK,
        bus, slot, func,
        g.vendor, g.device,
        0x02, 0x00, 0x00,
        g.bar0, 0,
        pci_cfg_read8(bus, slot, func, 0x3C)
    );

    return true;
}

bool ready() { return g.initialized; }
bool packet_io_ready() { return false; }
uint64_t mmio_base() { return g.bar0; }

void get_mac(uint8_t out[6])
{
    if (!out) return;
    for (int i = 0; i < 6; ++i)
        out[i] = g.mac[i];
}

uint16_t vendor_id() { return g.vendor; }
uint16_t device_id() { return g.device; }
const char* driver_name() { return "broadcom-tg3-probe"; }

} // namespace broadcom_network
