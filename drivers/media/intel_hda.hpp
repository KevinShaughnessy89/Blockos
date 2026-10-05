#pragma once
#include <stdint.h>

namespace intel_hda {

struct DeviceInfo {
    uint16_t vendor_id;
    uint16_t device_id;
    uint8_t codec_count;
    uint32_t codec_vendor_id;
};

bool init();
bool available();
bool probe(DeviceInfo& out);

} // namespace intel_hda
