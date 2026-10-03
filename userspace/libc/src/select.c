#include "sys/select.h"
#include "sys/poll.h"
#include "errno.h"
#include "unistd.h"
#include <string.h>
int select(int nfds, fd_set *rf, fd_set *wf, fd_set *ef, struct timeval *tv){
    if(nfds<0||nfds>FD_SETSIZE){errno=22;return -1;}
    struct pollfd pf[FD_SETSIZE]; int map[FD_SETSIZE]; size_t n=0;
    for(int fd=0;fd<nfds;++fd){short ev=0;if(rf&&FD_ISSET(fd,rf))ev|=POLLIN;if(wf&&FD_ISSET(fd,wf))ev|=POLLOUT;if(ef&&FD_ISSET(fd,ef))ev|=POLLERR;if(ev){pf[n].fd=fd;pf[n].events=ev;pf[n].revents=0;map[n]=(int)n;n++;}}
    int timeout=-1;if(tv){if(tv->tv_sec<0||tv->tv_usec<0){errno=22;return -1;}long long ms=(long long)tv->tv_sec*1000+tv->tv_usec/1000;timeout=ms>0x7fffffff?0x7fffffff:(int)ms;}
    int r=poll(pf,n,timeout);if(r<0)return -1;
    if(rf)FD_ZERO(rf);if(wf)FD_ZERO(wf);if(ef)FD_ZERO(ef);int ready=0;
    for(size_t i=0;i<n;i++)if(pf[i].revents){int fd=pf[i].fd;if((pf[i].revents&POLLIN)&&rf)FD_SET(fd,rf);if((pf[i].revents&POLLOUT)&&wf)FD_SET(fd,wf);if((pf[i].revents&(POLLERR|POLLHUP))&&ef)FD_SET(fd,ef);if((pf[i].revents&(POLLIN|POLLOUT|POLLERR|POLLHUP)))ready++;}
    return ready;
}
