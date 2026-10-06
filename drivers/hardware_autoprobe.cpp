#include "hardware_autoprobe.hpp"

#include "pci_subsystem.hpp"
#include "virtio_blk.hpp"
#include "virtio_net_driver.hpp"
#include "nvme.hpp"
#include "ahci.hpp"
#include "usb_xhci.hpp"
#include "audio.hpp"
#include "net/network_hw.hpp"

#include <string.h>

namespace blockos_hw {
namespace {

Report g_report{};

static void reset_report()
{
    memset(&g_report, 0, sizeof(g_report));
}

static void scan_pci()
{
    /* The scanner itself is side-effect-light and also populates the generic
       device registry used by VFS/device_manager. */
    pci_bus_manager.scan_all_pci_buses();
    pci_bus_manager.configure_mmio_bars();
    g_report.pci_devices = pci_bus_manager.device_count();
}

static void probe_storage()
{
    /* Prefer modern controllers.  ATA remains available as the legacy
       fallback through init_block_devices() in kernel.cpp. */
    g_report.virtio_block = virtio_blk::init();
    g_report.nvme = nvme::init();
    g_report.ahci = ahci::init();
}

static void probe_network()
{
    /* VirtIO is the preferred QEMU/server backend.  If it is absent, the
       native PCI backends handle common physical NICs such as Intel E1000
       and Realtek RTL8139. */
    g_report.virtio_net = virtio_net::init();
    g_report.native_net = network_hw::init();
}

static void probe_usb_audio()
{
    usb::xhci::Controller xhci{};
    if (usb::xhci::probe(&xhci)) {
        if (usb::xhci::reset(&xhci) && usb::xhci::run(&xhci))
            g_report.xhci = true;
    }

    g_report.hda = audio::init();
}
} // namespace

void pre_exit_bootstrap(Report* out_report)
{
    reset_report();

    scan_pci();
    probe_storage();
    probe_network();
    probe_usb_audio();

    if (out_report)
        *out_report = g_report;
}

const Report& last_report()
{
    return g_report;
}

} // namespace blockos_hw
