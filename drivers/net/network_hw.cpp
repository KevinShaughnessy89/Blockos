#include "network_hw.hpp"
#include "pci.hpp"
#include "intel_e1000.hpp"
#include "rtl8139.hpp"
#include "broadcom_network.hpp"

#include <string.h>

namespace network_hw {
namespace {

enum class Backend : uint8_t {
    None,
    E1000,
    RTL8139,
    Broadcom
};

DeviceInfo g_info{};
Backend g_backend = Backend::None;

static void set_identity(
    uint8_t bus,
    uint8_t slot,
    uint8_t func,
    uint16_t vendor,
    uint16_t device,
    uint64_t mmio,
    const char* name,
    bool packet_io)
{
    g_info = {};
    g_info.present = true;
    g_info.packet_io = packet_io;
    g_info.vendor = vendor;
    g_info.device = device;
    g_info.bus = bus;
    g_info.slot = slot;
    g_info.func = func;
    g_info.mmio = mmio;

    size_t i = 0;
    if (name) {
        for (; i + 1 < sizeof(g_info.driver) && name[i]; ++i)
            g_info.driver[i] = name[i];
    }
    g_info.driver[i] = '\0';
}

static bool try_e1000()
{
    for (uint32_t bus = 0; bus < 256; ++bus) {
        for (uint8_t slot = 0; slot < 32; ++slot) {
            for (uint8_t func = 0; func < 8; ++func) {
                if (!pci_device_exists(
                        static_cast<uint8_t>(bus), slot, func))
                    continue;

                const uint16_t vendor =
                    pci_cfg_read16(
                        static_cast<uint8_t>(bus), slot, func, 0);
                if (vendor != 0x8086)
                    continue;

                if (!intel_e1000::probe(
                        static_cast<uint8_t>(bus), slot, func))
                    continue;

                if (!intel_e1000::init(
                        static_cast<uint8_t>(bus), slot, func))
                    continue;

                set_identity(
                    static_cast<uint8_t>(bus), slot, func,
                    intel_e1000::vendor_id(), intel_e1000::device_id(),
                    pci_read_bar(static_cast<uint8_t>(bus), slot, func, 0),
                    "e1000", true);
                intel_e1000::get_mac(g_info.mac);
                g_backend = Backend::E1000;
                return true;
            }
        }
    }
    return false;
}

static bool try_rtl8139()
{
    for (uint32_t bus = 0; bus < 256; ++bus) {
        for (uint8_t slot = 0; slot < 32; ++slot) {
            for (uint8_t func = 0; func < 8; ++func) {
                if (!pci_device_exists(
                        static_cast<uint8_t>(bus), slot, func))
                    continue;

                if (!rtl8139::probe(
                        static_cast<uint8_t>(bus), slot, func))
                    continue;

                if (!rtl8139::init(
                        static_cast<uint8_t>(bus), slot, func))
                    continue;

                set_identity(
                    static_cast<uint8_t>(bus), slot, func,
                    rtl8139::vendor_id(), rtl8139::device_id(),
                    pci_read_bar(static_cast<uint8_t>(bus), slot, func, 0),
                    "rtl8139", true);
                rtl8139::get_mac(g_info.mac);
                g_backend = Backend::RTL8139;
                return true;
            }
        }
    }
    return false;
}

static bool try_broadcom()
{
    for (uint32_t bus = 0; bus < 256; ++bus) {
        for (uint8_t slot = 0; slot < 32; ++slot) {
            for (uint8_t func = 0; func < 8; ++func) {
                if (!pci_device_exists(
                        static_cast<uint8_t>(bus), slot, func))
                    continue;

                if (!broadcom_network::probe(
                        static_cast<uint8_t>(bus), slot, func))
                    continue;

                if (!broadcom_network::init(
                        static_cast<uint8_t>(bus), slot, func))
                    continue;

                set_identity(
                    static_cast<uint8_t>(bus), slot, func,
                    broadcom_network::vendor_id(), broadcom_network::device_id(),
                    broadcom_network::mmio_base(),
                    broadcom_network::driver_name(), false);
                broadcom_network::get_mac(g_info.mac);
                g_backend = Backend::Broadcom;
                return true;
            }
        }
    }
    return false;
}

} // namespace

bool init()
{
    if (ready())
        return true;

    g_backend = Backend::None;
    g_info = {};

    /* Prefer a native polling-capable NIC before the Broadcom probe. */
    if (try_e1000())
        return true;
    if (try_rtl8139())
        return true;
    if (try_broadcom())
        return true;

    return false;
}

bool ready()
{
    return g_backend != Backend::None && g_info.present;
}

bool send(const uint8_t* data, size_t length)
{
    switch (g_backend) {
        case Backend::E1000:
            return intel_e1000::send(data, length);
        case Backend::RTL8139:
            return rtl8139::send(data, length);
        default:
            return false;
    }
}

bool receive(uint8_t* buffer, size_t capacity, size_t* out_length)
{
    switch (g_backend) {
        case Backend::E1000:
            return intel_e1000::receive(buffer, capacity, out_length);
        case Backend::RTL8139:
            return rtl8139::receive(buffer, capacity, out_length);
        default:
            return false;
    }
}

void get_mac(uint8_t out[6])
{
    if (!out) return;
    memcpy(out, g_info.mac, sizeof(g_info.mac));
}

const DeviceInfo* info()
{
    return g_info.present ? &g_info : nullptr;
}

} // namespace network_hw
