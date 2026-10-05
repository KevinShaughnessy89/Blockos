#pragma once
#include <stdint.h>
#include "unistd.h"

typedef unsigned int tcflag_t;
typedef unsigned int speed_t;
typedef unsigned int cc_t;
struct termios {
    tcflag_t c_iflag;
    tcflag_t c_oflag;
    tcflag_t c_cflag;
    tcflag_t c_lflag;
    cc_t c_line;
    cc_t c_cc[32];
    speed_t c_ispeed;
    speed_t c_ospeed;
};

#define TIOCSCTTY 0x540E
#define TIOCGPGRP 0x540F
#define TIOCSPGRP 0x5410
#define TIOCGWINSZ 0x5413

pid_t tcgetpgrp(int fd);
int tcsetpgrp(int fd, pid_t pgrp);
int tcgetattr(int fd, struct termios* termios_p);
int tcsetattr(int fd, int optional_actions, const struct termios* termios_p);
