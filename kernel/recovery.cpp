#include "recovery.hpp"

namespace blockos::recovery {

namespace {
PersistFn g_load = nullptr;
PersistFn g_store = nullptr;
void* g_ctx = nullptr;
uint32_t g_attempts = 0;
uint32_t g_failure = 0;
BootStage g_stage = BootStage::Cold;
constexpr uint32_t MAX_BOOT_ATTEMPTS = 3;
uint32_t crc32_byte(uint32_t crc, uint8_t b) {
    crc ^= b;
    for (unsigned i = 0; i < 8; ++i) crc = (crc & 1u) ? (crc >> 1) ^ 0xEDB88320u : (crc >> 1);
    return crc;
}
void persist() {
    if (!g_store) return;
    const uint32_t c = crc32(&g_attempts, sizeof(g_attempts));
    g_store(g_ctx, g_attempts, g_stage, c);
}
}

void configure(PersistFn load, PersistFn store, void* ctx) {
    g_load = load; g_store = store; g_ctx = ctx;
    if (g_load) {
        uint32_t a = 0; BootStage s = BootStage::Cold; uint32_t c = 0;
        (void)a; (void)s; (void)c;
    }
}

void begin_boot() {
    if (g_attempts < 0xFFFFFFFFu) ++g_attempts;
    g_stage = BootStage::Bootloader;
    persist();
}

void mark_stage(BootStage s) { g_stage = s; persist(); }
void mark_failed(uint32_t reason) { g_failure = reason; g_stage = BootStage::Failed; persist(); }
void mark_ready() { g_stage = BootStage::Ready; g_attempts = 0; g_failure = 0; persist(); }
bool should_enter_recovery() { return g_stage == BootStage::Failed || g_attempts >= MAX_BOOT_ATTEMPTS; }
uint32_t boot_attempts() { return g_attempts; }
uint32_t last_failure() { return g_failure; }
BootStage stage() { return g_stage; }
void clear_failure() { g_failure = 0; if (g_stage == BootStage::Failed) g_stage = BootStage::Kernel; persist(); }

uint32_t crc32(const void* data, size_t len, uint32_t seed) {
    if (!data) return seed ^ 0xFFFFFFFFu;
    const uint8_t* p = static_cast<const uint8_t*>(data);
    uint32_t crc = seed;
    for (size_t i = 0; i < len; ++i) crc = crc32_byte(crc, p[i]);
    return crc ^ 0xFFFFFFFFu;
}

}
