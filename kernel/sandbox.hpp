#pragma once
#include <stdint.h>
#include <stddef.h>

namespace blockos::sandbox {

enum Rights : uint32_t { FileRead=1u<<0, FileWrite=1u<<1, Net=1u<<2, Exec=1u<<3, Device=1u<<4, Admin=1u<<31 };
struct Policy { uint64_t token; uint32_t rights; uint32_t flags; char root[128]; bool active; };

uint64_t create(uint32_t rights, const char* root);
bool destroy(uint64_t token);
bool allow(uint64_t token, uint32_t right);
bool check(uint64_t token, uint32_t right);
bool path_allowed(uint64_t token, const char* path, size_t len);

}
