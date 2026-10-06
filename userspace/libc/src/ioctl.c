#include "sys/ioctl.h"
#include "termios.h"
#include "fcntl.h"
#include "unistd.h"
#include "errno.h"
#include "blockos_syscall.h"
#include <stdarg.h>
int ioctl(int fd,unsigned long req,...){va_list ap;void*arg;va_start(ap,req);arg=va_arg(ap,void*);va_end(ap);
 if(req==FIONBIO)return fcntl(fd,F_SETFL,(*(int*)arg)?O_NONBLOCK:0);
 if(req==TIOCGWINSZ){struct winsize*w=(struct winsize*)arg;if(!w){errno=14;return -1;}w->ws_row=24;w->ws_col=80;w->ws_xpixel=0;w->ws_ypixel=0;return 0;}
 if(req==TIOCSWINSZ||req==TIOCGETA||req==TIOCSETA||req==TIOCSPGRP||req==TCFLSH)return 0;
 if(req==TIOCGPGRP){if(arg)*(pid_t*)arg=getpgrp();return 0;}
 long r=__blockos_syscall(__SYS_ioctl,fd,req,(long)arg,0,0,0);if(r<0){errno=(int)-r;return -1;}return (int)r;}
