#include "sys/ioctl.h"
#include "blockos_syscall.h"
#include "errno.h"
#include <stdarg.h>

int ioctl(int fd, unsigned long request, ...) {
    va_list ap;
    va_start(ap, request);
    void* arg = va_arg(ap, void*);
    va_end(ap);
    long r = __blockos_syscall(__SYS_ioctl, fd, (long)request, (long)arg, 0, 0, 0);
    if (r < 0) { errno = (int)-r; return -1; }
    return (int)r;
}
