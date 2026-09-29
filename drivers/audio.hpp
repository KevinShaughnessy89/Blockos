#pragma once

#include <cstdint>

namespace audio {

bool init();
bool is_ready();
bool hda_present();
uint16_t codec_mask();
uint32_t codec_vendor_id();

/* Minimal emergency/fallback output using the legacy PC speaker. */
bool speaker_beep(uint32_t frequency_hz, uint32_t milliseconds);
void speaker_stop();

} // namespace audio
