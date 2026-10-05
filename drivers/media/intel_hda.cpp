#include "intel_hda.hpp"
#include "../audio.hpp"

namespace intel_hda {
namespace { DeviceInfo g{}; bool ready=false; }

bool init() {
    if (ready) return true;
    if (!audio::init() || !audio::hda_present()) return false;
    // The existing audio driver performs the real PCI HDA controller reset,
    // CORB/RIRB DMA setup and codec GET_PARAMETER transaction.
    g.vendor_id = 0;
    g.device_id = 0;
    g.codec_count = 0;
    g.codec_vendor_id = audio::codec_vendor_id();
    g.codec_count = 0;
    uint16_t mask = audio::codec_mask();
    for (uint8_t i=0; i<16; ++i) if (mask & (1u<<i)) ++g.codec_count;
    ready = true;
    return true;
}

bool available(){ return ready; }
bool probe(DeviceInfo& out){ if(!ready) return false; out=g; return true; }

} // namespace intel_hda
