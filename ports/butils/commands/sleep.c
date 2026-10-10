#include <stdlib.h>
#include <unistd.h>
#include "blockos_syscall.h"
int main(int argc,char**argv){if(argc!=2)return 2;char*end=0;long n=strtol(argv[1],&end,10);if(!argv[1][0]||!end||*end||n<0)return 2;struct timespec ts;ts.tv_sec=n;ts.tv_nsec=0;return __blockos_syscall(__SYS_nanosleep,(long)&ts,0,0,0,0,0)<0?1:0;}
