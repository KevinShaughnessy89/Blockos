#pragma once
#include <stdint.h>
#include <stddef.h>

namespace wifi {

enum class Vendor : uint8_t {
    Unknown = 0,
    Intel,
    Realtek,
    MediaTek,
    Broadcom
};

struct DeviceInfo {
    uint8_t bus;
    uint8_t slot;
    uint8_t function;
    uint16_t vendor_id;
    uint16_t device_id;
    Vendor vendor;
    bool present;
    bool firmware_required;
};

/* Kept backward-compatible with the original five callbacks. */
struct Ops {
    bool (*scan)(void*, void*, size_t);
    bool (*associate)(void*, const char*, const char*);
    bool (*send)(void*, const void*, size_t);
    int  (*recv)(void*, void*, size_t);
    void* ctx;

    /* Optional hardware-specific extensions. */
    bool (*load_firmware)(void*, const void*, size_t);
    bool (*start)(void*);
    void (*stop)(void*);
    const char* driver_name;
};

bool init();
bool available();
bool register_ops(const Ops& ops);
bool load_firmware(const void* image, size_t size);
bool start();
void stop();
bool associate(const char* ssid, const char* passphrase);
bool send(const void* frame, size_t len);
int receive(void* frame, size_t cap);

const DeviceInfo& device_info();
Vendor vendor();
const char* vendor_name();
const char* recommended_firmware();
const char* driver_name();
uint16_t vendor_id();
uint16_t device_id();

}
