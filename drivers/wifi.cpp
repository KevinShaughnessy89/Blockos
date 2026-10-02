#include "wifi.hpp"
#include "pci.hpp"
#include <string.h>
#include <stdio.h>

namespace wifi {
namespace {
    Ops g_ops{};
    DeviceInfo g_device{};
    bool g_scanned = false;
    bool g_started = false;
    const char* g_driver_name = nullptr;
    char g_firmware_path[128]{};

    Vendor classify_vendor(uint16_t id) {
        switch (id) {
            case 0x8086: return Vendor::Intel;
            case 0x10EC: return Vendor::Realtek;
            case 0x14C3: return Vendor::MediaTek;
            case 0x14E4: return Vendor::Broadcom;
            default: return Vendor::Unknown;
        }
    }

    const char* vendor_string(Vendor v) {
        switch (v) {
            case Vendor::Intel: return "Intel";
            case Vendor::Realtek: return "Realtek";
            case Vendor::MediaTek: return "MediaTek";
            case Vendor::Broadcom: return "Broadcom";
            default: return "Unknown";
        }
    }

    void build_firmware_path() {
        const char* vendor_dir = "generic";
        switch (g_device.vendor) {
            case Vendor::Intel: vendor_dir = "intel"; break;
            case Vendor::Realtek: vendor_dir = "realtek"; break;
            case Vendor::MediaTek: vendor_dir = "mediatek"; break;
            case Vendor::Broadcom: vendor_dir = "broadcom"; break;
            default: break;
        }
        (void)snprintf(g_firmware_path, sizeof(g_firmware_path),
                       "/system/firmware/wifi/%s/%04x-%04x.fw",
                       vendor_dir,
                       (unsigned)g_device.vendor_id,
                       (unsigned)g_device.device_id);
    }

    bool scan_pci() {
        for (uint16_t bus = 0; bus < 256; ++bus) {
            for (uint8_t slot = 0; slot < 32; ++slot) {
                for (uint8_t function = 0; function < 8; ++function) {
                    const uint16_t vendor_id =
                        pci_cfg_read16((uint8_t)bus, slot, function, 0);
                    if (vendor_id == 0xFFFF)
                        continue;

                    const uint8_t class_code =
                        pci_cfg_read8((uint8_t)bus, slot, function, 0x0B);
                    const uint8_t subclass =
                        pci_cfg_read8((uint8_t)bus, slot, function, 0x0A);

                    /* 0x02 = Network controller, 0x80 = other network controller. */
                    if (class_code != 0x02 || subclass != 0x80)
                        continue;

                    g_device.bus = (uint8_t)bus;
                    g_device.slot = slot;
                    g_device.function = function;
                    g_device.vendor_id = vendor_id;
                    g_device.device_id =
                        pci_cfg_read16((uint8_t)bus, slot, function, 2);
                    g_device.vendor = classify_vendor(vendor_id);
                    g_device.present = true;
                    g_device.firmware_required = true;
                    build_firmware_path();
                    return true;
                }
            }
        }
        return false;
    }
}

bool init() {
    if (g_scanned)
        return g_device.present;
    g_scanned = true;
    return scan_pci();
}

bool available() {
    return g_device.present && g_started &&
           g_ops.associate && g_ops.send && g_ops.recv;
}

bool register_ops(const Ops& ops) {
    if (!ops.associate || !ops.send || !ops.recv)
        return false;
    g_ops = ops;
    g_driver_name = ops.driver_name;
    return true;
}

bool load_firmware(const void* image, size_t size) {
    if (!image || !size || !g_ops.load_firmware)
        return false;
    return g_ops.load_firmware(g_ops.ctx, image, size);
}

bool start() {
    if (!g_device.present || !g_ops.associate || !g_ops.send || !g_ops.recv)
        return false;
    if (g_started)
        return true;
    if (g_ops.start && !g_ops.start(g_ops.ctx))
        return false;
    g_started = true;
    return true;
}

void stop() {
    if (!g_started)
        return;
    if (g_ops.stop)
        g_ops.stop(g_ops.ctx);
    g_started = false;
}

bool associate(const char* ssid, const char* passphrase) {
    if (!available() || !ssid || !passphrase)
        return false;
    const size_t ssid_len = strlen(ssid);
    const size_t pass_len = strlen(passphrase);
    if (ssid_len == 0 || ssid_len > 32 || pass_len < 8 || pass_len > 63)
        return false;
    return g_ops.associate(g_ops.ctx, ssid, passphrase);
}

bool send(const void* frame, size_t len) {
    return available() && frame && len && g_ops.send(g_ops.ctx, frame, len);
}

int receive(void* frame, size_t cap) {
    return available() && frame && cap ? g_ops.recv(g_ops.ctx, frame, cap) : -1;
}

const DeviceInfo& device_info() { return g_device; }
Vendor vendor() { return g_device.vendor; }
const char* vendor_name() { return vendor_string(g_device.vendor); }
const char* recommended_firmware() { return g_firmware_path; }
const char* driver_name() { return g_driver_name ? g_driver_name : "unbound"; }
uint16_t vendor_id() { return g_device.vendor_id; }
uint16_t device_id() { return g_device.device_id; }

}
