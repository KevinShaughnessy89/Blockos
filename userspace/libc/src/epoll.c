#include "sys/epoll.h"
#include "blockos_syscall.h"
#include "errno.h"
#include "sys/time.h"
#include "unistd.h"
#include <stdint.h>
int epoll_create1(int flags){long r=__blockos_syscall(__SYS_epoll_create1,flags,0,0,0,0,0);if(r<0){errno=(int)-r;return -1;}return (int)r;}
int epoll_ctl(int e,int op,int fd,struct epoll_event*ev){long r=__blockos_syscall(__SYS_epoll_ctl,e,op,fd,0,0,(long)ev);if(r<0){errno=(int)-r;return -1;}return 0;}
int epoll_wait(int e,struct epoll_event*ev,int n,int timeout){if(timeout==0){long r=__blockos_syscall(__SYS_epoll_wait,e,(long)ev,n,0,0,0);if(r<0){errno=(int)-r;return -1;}return (int)r;}uint64_t start=0;struct timespec ts;if(clock_gettime(CLOCK_MONOTONIC,&ts)==0)start=(uint64_t)ts.tv_sec*1000u+ts.tv_nsec/1000000u;for(;;){long r=__blockos_syscall(__SYS_epoll_wait,e,(long)ev,n,0,0,0);if(r<0){errno=(int)-r;return -1;}if(r>0)return (int)r;if(timeout>0){struct timespec now;clock_gettime(CLOCK_MONOTONIC,&now);uint64_t cur=(uint64_t)now.tv_sec*1000u+now.tv_nsec/1000000u;if(cur-start>=(uint64_t)timeout)return 0;}struct timespec sl={0,1000000};nanosleep(&sl,0);}}
