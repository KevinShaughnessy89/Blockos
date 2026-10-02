#pragma once
#include <stdint.h>

namespace blockos::permissions {

enum Access : uint8_t { Read=1, Write=2, Execute=4 };
struct Credentials { uint32_t uid; uint32_t gid; };
struct Metadata { uint32_t mode; uint32_t uid; uint32_t gid; };

bool allowed(const Metadata& meta, Credentials cred, uint8_t access);
bool chmod(Metadata& meta, Credentials cred, uint32_t mode);
bool chown(Metadata& meta, Credentials cred, uint32_t uid, uint32_t gid);

}
