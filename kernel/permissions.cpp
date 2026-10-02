#include "permissions.hpp"

namespace blockos::permissions {
static uint8_t bits(uint32_t mode, unsigned shift) { return uint8_t((mode >> shift) & 7u); }
bool allowed(const Metadata& m, Credentials c, uint8_t a) {
    if (c.uid == 0) return true;
    unsigned shift = c.uid == m.uid ? 6 : (c.gid == m.gid ? 3 : 0);
    uint8_t b = bits(m.mode, shift);
    if ((a & Read) && !(b & 4)) return false;
    if ((a & Write) && !(b & 2)) return false;
    if ((a & Execute) && !(b & 1)) return false;
    return true;
}
bool chmod(Metadata& m, Credentials c, uint32_t mode) { if (c.uid != 0 && c.uid != m.uid) return false; m.mode = (m.mode & 0170000u) | (mode & 07777u); return true; }
bool chown(Metadata& m, Credentials c, uint32_t uid, uint32_t gid) { if (c.uid != 0) return false; m.uid = uid; m.gid = gid; return true; }
}
