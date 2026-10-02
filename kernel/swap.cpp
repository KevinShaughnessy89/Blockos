#include "swap.hpp"

namespace blockos::swap {
namespace {
Device g_dev{};
bool g_enabled = false;
constexpr uint32_t MAX_SLOTS = 8192;
uint8_t g_bitmap[MAX_SLOTS / 8]{};
bool bit(uint32_t n) { return (g_bitmap[n >> 3] >> (n & 7)) & 1u; }
void setbit(uint32_t n, bool v) { uint8_t& b = g_bitmap[n >> 3]; const uint8_t m = uint8_t(1u << (n & 7)); if (v) b |= m; else b &= uint8_t(~m); }
}

bool init(const Device& dev) {
    if (!dev.read || !dev.write || dev.block_size == 0 || dev.slots == 0 || dev.slots > MAX_SLOTS) return false;
    g_dev = dev; g_enabled = true;
    for (size_t i = 0; i < sizeof(g_bitmap); ++i) g_bitmap[i] = 0;
    return true;
}
bool enabled() { return g_enabled; }
int64_t allocate_slot() { if (!g_enabled) return -1; for (uint32_t i = 0; i < g_dev.slots; ++i) if (!bit(i)) { setbit(i,true); return i; } return -1; }
bool free_slot(uint32_t slot) { if (!g_enabled || slot >= g_dev.slots || !bit(slot)) return false; setbit(slot,false); return true; }
bool read_slot(uint32_t slot, void* out, size_t bytes) { if (!g_enabled || !out || slot >= g_dev.slots || bytes > g_dev.block_size) return false; return g_dev.read(g_dev.ctx, g_dev.start_block + slot, out, bytes); }
bool write_slot(uint32_t slot, const void* data, size_t bytes) { if (!g_enabled || !data || slot >= g_dev.slots || bytes > g_dev.block_size || !bit(slot)) return false; return g_dev.write(g_dev.ctx, g_dev.start_block + slot, data, bytes); }
uint32_t slot_count() { return g_dev.slots; }
}
