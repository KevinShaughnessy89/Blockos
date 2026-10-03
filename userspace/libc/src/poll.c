#include "sys/poll.h"
#include "blockos_syscall.h"
#include "errno.h"
#include "sys/time.h"
#include "unistd.h"

static uint64_t ms_now(void){struct timespec ts;if(clock_gettime(CLOCK_MONOTONIC,&ts)<0)return 0;return (uint64_t)ts.tv_sec*1000u+(uint64_t)ts.tv_nsec/1000000u;}
int poll(struct pollfd* fds,size_t nfds,int timeout){
    if(timeout==0){long r=__blockos_syscall(__SYS_poll,(long)fds,nfds,0,0,0,0);if(r<0){errno=(int)-r;return -1;}return (int)r;}
    uint64_t start=ms_now();
    for(;;){long r=__blockos_syscall(__SYS_poll,(long)fds,nfds,0,0,0,0);if(r<0){errno=(int)-r;return -1;}if(r>0)return (int)r;if(timeout>0){uint64_t elapsed=ms_now()-start;if(elapsed>=(uint64_t)timeout)return 0;}struct timespec sl={0,1000000};nanosleep(&sl,0);}
}
