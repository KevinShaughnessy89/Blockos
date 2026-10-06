#pragma once
#include <stdint.h>
typedef unsigned int tcflag_t;typedef unsigned char cc_t;typedef unsigned int speed_t;
#define NCCS 20
struct termios{tcflag_t c_iflag,c_oflag,c_cflag,c_lflag;cc_t c_line;cc_t c_cc[NCCS];speed_t c_ispeed,c_ospeed;};
#define VINTR 0
#define VQUIT 1
#define VERASE 2
#define VKILL 3
#define VEOF 4
#define VTIME 5
#define VMIN 6
#define VSTART 8
#define VSTOP 9
#define ISIG 0000001u
#define ICANON 0000002u
#define ECHO 0000010u
#define ECHOE 0000020u
#define IEXTEN 0100000u
#define IXON 0002000u
#define IXOFF 0010000u
#define OPOST 0000001u
#define CS8 0000060u
#define CREAD 0000200u
#define PARENB 0000400u
#define PARODD 0001000u
#define CLOCAL 0004000u
#define TCSANOW 0
#define TCSADRAIN 1
#define TCSAFLUSH 2
#define TCIFLUSH 0
#define TCOFLUSH 1
#define TCIOFLUSH 2
#define B0 0
#define B9600 13
#define B19200 14
#define B38400 15
#define B57600 4097
#define B115200 4098
int tcgetattr(int,struct termios*);int tcsetattr(int,int,const struct termios*);int tcflush(int,int);
speed_t cfgetispeed(const struct termios*);speed_t cfgetospeed(const struct termios*);int cfsetispeed(struct termios*,speed_t);int cfsetospeed(struct termios*,speed_t);

#define IXON 0x0400
#define OPOST 0x0001
#define CLOCAL 0x0800
#define CREAD 0x0080
#define CS8 0x0030
#define ISIG 0x0001
#define ICANON 0x0002
#define ECHO 0x0008
#define ECHOE 0x0010
#define IEXTEN 0x8000
#define VMIN 5
#define VTIME 6
#define B38400 38400