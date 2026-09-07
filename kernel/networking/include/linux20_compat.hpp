#pragma once

// Compatibility boundary for the historical Linux 2.0 networking sources.
// This header deliberately does not fake kernel internals.  Individual
// protocol modules must bind their device/socket/memory primitives to the
// BlockOS implementations before being enabled in a production build.

#include <cstddef>
#include <cstdint>
#include "linux20_blockos_adapter.hpp"

namespace blockos::network::linux20 {
using u8  = std::uint8_t;
using u16 = std::uint16_t;
using u32 = std::uint32_t;
using s8  = std::int8_t;
using s16 = std::int16_t;
using s32 = std::int32_t;
}
