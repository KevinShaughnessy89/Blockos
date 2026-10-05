#include "termios.h"
#include "sys/ioctl.h"
#include "errno.h"

pid_t tcgetpgrp(int fd) {
    (void)fd;
    return getpgrp();
}

int tcsetpgrp(int fd, pid_t pgrp) {
    (void)fd; (void)pgrp;
    return 0;
}

int tcgetattr(int fd, struct termios* termios_p) {
    (void)fd;
    if (!termios_p) { errno = 14; return -1; }
    for (unsigned i = 0; i < sizeof(*termios_p); ++i)
        ((unsigned char*)termios_p)[i] = 0;
    return 0;
}

int tcsetattr(int fd, int optional_actions, const struct termios* termios_p) {
    (void)fd; (void)optional_actions; (void)termios_p;
    return 0;
}
