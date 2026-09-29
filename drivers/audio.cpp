#include "audio.hpp"
#include "pci.hpp"
#include "device_manager.hpp"
#include "dma.hpp"

extern "C" {
#include <efi.h>
#include <efilib.h>
}

#include <stdint.h>
#include <stddef.h>
#include <string.h>

namespace audio {
namespace {

constexpr uint8_t PCI_CLASS_MULTIMEDIA = 0x04;
constexpr uint8_t PCI_SUBCLASS_AUDIO = 0x03;
constexpr uint32_t HDA_GCAP = 0x00;
constexpr uint32_t HDA_GCTL = 0x08;
constexpr uint32_t HDA_STATESTS = 0x0E;
constexpr uint32_t HDA_CORBLBASE = 0x40;
constexpr uint32_t HDA_CORBUBASE = 0x44;
constexpr uint32_t HDA_CORBWP = 0x48;
constexpr uint32_t HDA_CORBRP = 0x4A;
constexpr uint32_t HDA_CORBSIZE = 0x4E;
constexpr uint32_t HDA_RIRBLBASE = 0x50;
constexpr uint32_t HDA_RIRBUBASE = 0x54;
constexpr uint32_t HDA_RIRBWP = 0x58;
constexpr uint32_t HDA_RINTCNT = 0x5A;
constexpr uint32_t HDA_RIRBCTL = 0x5C;
constexpr uint32_t HDA_RIRBSIZE = 0x5E;
constexpr uint32_t HDA_INTCTL = 0x20;

constexpr uint32_t HDA_GCTL_CRST = 1u << 0;
constexpr uint16_t CORB_RESET = 1u << 15;
constexpr uint16_t RIRB_RESET = 1u << 15;

struct State {
    bool ready = false;
    bool hda = false;
    uint8_t bus = 0;
    uint8_t slot = 0;
    uint8_t func = 0;
    uint16_t vendor = 0xFFFF;
    uint16_t device = 0;
    uint64_t bar0 = 0;
    uint16_t codecs = 0;
    uint32_t codec_id = 0;
    uint16_t corb_entries = 0;
    uint16_t rirb_entries = 0;
    uint16_t rirb_head = 0;
    void* corb = nullptr;
    void* rirb = nullptr;
};

State g{};

static inline volatile uint8_t* reg8(uint64_t off)
{
    return reinterpret_cast<volatile uint8_t*>(static_cast<uintptr_t>(g.bar0 + off));
}

static inline volatile uint16_t* reg16(uint64_t off)
{
    return reinterpret_cast<volatile uint16_t*>(static_cast<uintptr_t>(g.bar0 + off));
}

static inline volatile uint32_t* reg32(uint64_t off)
{
    return reinterpret_cast<volatile uint32_t*>(static_cast<uintptr_t>(g.bar0 + off));
}

static inline uint8_t rd8(uint64_t off) { return *reg8(off); }
static inline uint16_t rd16(uint64_t off) { return *reg16(off); }
static inline uint32_t rd32(uint64_t off) { return *reg32(off); }
static inline void wr8(uint64_t off, uint8_t v) { *reg8(off) = v; }
static inline void wr16(uint64_t off, uint16_t v) { *reg16(off) = v; }
static inline void wr32(uint64_t off, uint32_t v) { *reg32(off) = v; }

static uint64_t find_controller()
{
    for (uint32_t bus = 0; bus < 256; ++bus) {
        for (uint8_t slot = 0; slot < 32; ++slot) {
            const uint16_t v0 = pci_cfg_read16((uint8_t)bus, slot, 0, 0x00);
            if (v0 == 0xFFFF)
                continue;
            uint8_t functions = (pci_cfg_read8((uint8_t)bus, slot, 0, 0x0E) & 0x80) ? 8 : 1;
            for (uint8_t func = 0; func < functions; ++func) {
                const uint16_t vendor = pci_cfg_read16((uint8_t)bus, slot, func, 0x00);
                if (vendor == 0xFFFF)
                    continue;
                if (pci_cfg_read8((uint8_t)bus, slot, func, 0x0B) != PCI_CLASS_MULTIMEDIA ||
                    pci_cfg_read8((uint8_t)bus, slot, func, 0x0A) != PCI_SUBCLASS_AUDIO)
                    continue;
                uint64_t bar = pci_read_bar((uint8_t)bus, slot, func, 0);
                if (!bar || (bar & 1ULL))
                    continue;
                uint16_t command = pci_cfg_read16((uint8_t)bus, slot, func, 0x04);
                command |= 0x0006;
                pci_cfg_write16((uint8_t)bus, slot, func, 0x04, command);
                g.bus = (uint8_t)bus;
                g.slot = slot;
                g.func = func;
                g.vendor = vendor;
                g.device = pci_cfg_read16((uint8_t)bus, slot, func, 0x02);
                return bar;
            }
        }
    }
    return 0;
}

static bool controller_reset()
{
    uint32_t gctl = rd32(HDA_GCTL);
    gctl &= ~HDA_GCTL_CRST;
    wr32(HDA_GCTL, gctl);
    for (uint32_t i = 0; i < 500000; ++i) {
        if ((rd32(HDA_GCTL) & HDA_GCTL_CRST) == 0)
            break;
        __asm__ volatile("pause");
    }
    wr32(HDA_GCTL, gctl | HDA_GCTL_CRST);
    for (uint32_t i = 0; i < 500000; ++i) {
        if (rd32(HDA_GCTL) & HDA_GCTL_CRST)
            return true;
        __asm__ volatile("pause");
    }
    return false;
}

static uint16_t select_ring_size(uint8_t value, uint16_t preferred)
{
    const uint8_t supported = value & 0x70;
    if (preferred == 256 && (supported & 0x40)) return 256;
    if ((preferred >= 16) && (supported & 0x20)) return 16;
    if (supported & 0x10) return 2;
    return 0;
}

static bool setup_corb_rirb()
{
    const uint8_t corb_size = rd8(HDA_CORBSIZE);
    const uint8_t rirb_size = rd8(HDA_RIRBSIZE);

    g.corb_entries = select_ring_size(corb_size, 256);
    const uint16_t requested_rirb = 256;
    g.rirb_entries = select_ring_size(rirb_size, requested_rirb);
    if (g.rirb_entries == 0) return false;
    if (g.corb_entries == 0)
        return false;

    g.corb = dma::alloc(static_cast<size_t>(g.corb_entries) * 4u, 128);
    g.rirb = dma::alloc(static_cast<size_t>(g.rirb_entries) * 8u, 128);
    if (!g.corb || !g.rirb)
        return false;
    memset(g.corb, 0, static_cast<size_t>(g.corb_entries) * 4u);
    memset(g.rirb, 0, static_cast<size_t>(g.rirb_entries) * 8u);

    uint16_t corb_sel = 0;
    if (g.corb_entries == 256) corb_sel = 2;
    else if (g.corb_entries == 16) corb_sel = 1;
    else corb_sel = 0;
    wr8(HDA_CORBSIZE, static_cast<uint8_t>((corb_size & 0x70) | corb_sel));
    uint16_t rirb_sel = (g.rirb_entries == 256) ? 2 : ((g.rirb_entries == 16) ? 1 : 0);
    wr8(HDA_RIRBSIZE, static_cast<uint8_t>((rirb_size & 0x70) | rirb_sel));

    wr32(HDA_CORBLBASE, static_cast<uint32_t>(reinterpret_cast<uint64_t>(g.corb)));
    wr32(HDA_CORBUBASE, static_cast<uint32_t>(reinterpret_cast<uint64_t>(g.corb) >> 32));
    wr16(HDA_CORBRP, CORB_RESET);
    wr16(HDA_CORBRP, 0);
    wr16(HDA_CORBWP, 0);

    wr32(HDA_RIRBLBASE, static_cast<uint32_t>(reinterpret_cast<uint64_t>(g.rirb)));
    wr32(HDA_RIRBUBASE, static_cast<uint32_t>(reinterpret_cast<uint64_t>(g.rirb) >> 32));
    wr16(HDA_RIRBWP, RIRB_RESET);
    wr16(HDA_RINTCNT, 1);
    wr8(HDA_RIRBCTL, 0x02); // RIRB DMA enable
    g.rirb_head = 0;
    return true;
}

static bool codec_command(uint8_t cad, uint8_t nid, uint16_t verb, uint8_t param, uint32_t* response)
{
    if (!g.corb || !g.rirb || g.corb_entries == 0)
        return false;

    uint16_t wp = rd16(HDA_CORBWP) & 0xFF;
    uint16_t next = static_cast<uint16_t>((wp + 1) % g.corb_entries);
    if (next == (rd16(HDA_CORBRP) & 0xFF))
        return false;

    const uint32_t cmd = (uint32_t(cad) << 28) |
                         (uint32_t(nid) << 20) |
                         (uint32_t(verb) << 8) |
                         uint32_t(param);
    reinterpret_cast<volatile uint32_t*>(g.corb)[wp] = cmd;
    __asm__ volatile("sfence" ::: "memory");
    wr16(HDA_CORBWP, next);

    for (uint32_t i = 0; i < 500000; ++i) {
        const uint16_t hw_wp = rd16(HDA_RIRBWP) & 0xFF;
        if (hw_wp != g.rirb_head) {
            g.rirb_head = static_cast<uint16_t>((g.rirb_head + 1) % g.rirb_entries);
            const uint64_t* ring = reinterpret_cast<const uint64_t*>(g.rirb);
            if (response)
                *response = static_cast<uint32_t>(ring[g.rirb_head]);
            return true;
        }
        __asm__ volatile("pause");
    }
    return false;
}

} // namespace

bool init()
{
    if (g.ready)
        return true;

    g.bar0 = find_controller();
    if (!g.bar0)
        return false;
    g.hda = true;

    if (!controller_reset() || !setup_corb_rirb()) {
        g.hda = false;
        return false;
    }

    g.codecs = static_cast<uint16_t>(rd16(HDA_STATESTS) & 0xFF);
    if (g.codecs != 0) {
        const uint8_t cad = 0;
        if (g.codecs & 1u)
            codec_command(cad, 0, 0xF00, 0x00, &g.codec_id);
    }

    wr32(HDA_INTCTL, 0);
    g.ready = true;

    hardware_center.register_pci_device(
        "hdaudio0",
        DEV_TYPE_AUDIO,
        g.bus,
        g.slot,
        g.func,
        g.vendor,
        g.device,
        PCI_CLASS_MULTIMEDIA,
        PCI_SUBCLASS_AUDIO,
        0,
        g.bar0,
        0,
        0);

    Print((CHAR16*)L"audio: Intel HDA controller online\n");
    return true;
}

bool is_ready() { return g.ready; }
bool hda_present() { return g.hda; }
uint16_t codec_mask() { return g.codecs; }
uint32_t codec_vendor_id() { return g.codec_id; }

bool speaker_beep(uint32_t frequency_hz, uint32_t milliseconds)
{
    if (frequency_hz < 20 || frequency_hz > 20000 || milliseconds == 0)
        return false;

    const uint32_t divisor = 1193182u / frequency_hz;
    if (divisor == 0 || divisor > 0xFFFFu)
        return false;

    asm volatile("outb %0, %1" : : "a"(0xB6), "Nd"((uint16_t)0x43));
    asm volatile("outb %0, %1" : : "a"(uint8_t(divisor & 0xFF)), "Nd"((uint16_t)0x42));
    asm volatile("outb %0, %1" : : "a"(uint8_t(divisor >> 8)), "Nd"((uint16_t)0x42));

    uint8_t gate = 0;
    asm volatile("inb %1, %0" : "=a"(gate) : "Nd"((uint16_t)0x61));
    gate |= 0x03;
    asm volatile("outb %0, %1" : : "a"(gate), "Nd"((uint16_t)0x61));

    /* Busy wait is only a fallback diagnostic path; HDA is the primary device. */
    for (uint64_t i = 0; i < uint64_t(milliseconds) * 200000ULL; ++i)
        __asm__ volatile("pause");

    speaker_stop();
    return true;
}

void speaker_stop()
{
    uint8_t gate = 0;
    asm volatile("inb %1, %0" : "=a"(gate) : "Nd"((uint16_t)0x61));
    gate &= ~0x03;
    asm volatile("outb %0, %1" : : "a"(gate), "Nd"((uint16_t)0x61));
}

} // namespace audio
