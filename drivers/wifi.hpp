#pragma once
#include <stdint.h>
#include <stddef.h>
namespace wifi {
struct Ops { bool (*scan)(void*,void*,size_t); bool (*associate)(void*,const char*,const char*); bool (*send)(void*,const void*,size_t); int (*recv)(void*,void*,size_t); void* ctx; };
bool init(); bool available(); bool register_ops(const Ops& ops); bool associate(const char* ssid,const char* passphrase); bool send(const void* frame,size_t len); int receive(void* frame,size_t cap); uint16_t vendor_id(); uint16_t device_id();
}
