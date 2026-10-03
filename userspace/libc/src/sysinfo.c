#include "sys/utsname.h"
#include "sys/resource.h"
#include "sys/time.h"
#include "errno.h"
#include "unistd.h"
#include "string.h"

int uname(struct utsname *u){
    if(!u){errno=14;return -1;}
    memset(u,0,sizeof(*u));
    strcpy(u->sysname,"BlockOS"); strcpy(u->nodename,"blockos");
    strcpy(u->release,"1.0"); strcpy(u->version,"BlockOS userspace");
    strcpy(u->machine,"x86_64"); strcpy(u->domainname,"local"); return 0;
}
int getrusage(int who, struct rusage *u){(void)who;if(!u){errno=14;return -1;}memset(u,0,sizeof(*u));struct timespec ts;clock_gettime(CLOCK_PROCESS_CPUTIME_ID,&ts);u->ru_utime.tv_sec=ts.tv_sec;u->ru_utime.tv_usec=ts.tv_nsec/1000;return 0;}
