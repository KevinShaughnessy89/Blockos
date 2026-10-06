#include "time.h"
#include "sys/time.h"
#include "blockos_syscall.h"
#include "errno.h"
time_t time(time_t*t){struct timespec ts;if(clock_gettime(CLOCK_REALTIME,&ts)<0)return(time_t)-1;if(t)*t=(time_t)ts.tv_sec;return(time_t)ts.tv_sec;}
int clock_gettime(clockid_t id,struct timespec*t){long r=__blockos_syscall(__SYS_clock_gettime,id,(long)t,0,0,0,0);if(r<0){errno=(int)-r;return-1;}return(int)r;}
int gettimeofday(struct timeval*t,struct timezone*z){struct timespec ts;(void)z;if(!t||clock_gettime(CLOCK_REALTIME,&ts)<0)return-1;t->tv_sec=ts.tv_sec;t->tv_usec=ts.tv_nsec/1000;return 0;}
