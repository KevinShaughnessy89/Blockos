#include <dirent.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
static int number(const char*s){if(!*s)return 0;while(*s){if(*s<'0'||*s>'9')return 0;s++;}return 1;}
int main(void){DIR*d=opendir("/proc");if(!d){write(2,"ps: /proc unavailable\n",22);return 1;}printf("PID COMMAND\n");struct dirent*e;while((e=readdir(d))){if(!number(e->d_name))continue;char p[256],b[256];snprintf(p,sizeof p,"/proc/%s/comm",e->d_name);int fd=open(p,O_RDONLY);if(fd<0){snprintf(p,sizeof p,"/proc/%s/cmdline",e->d_name);fd=open(p,O_RDONLY);}if(fd<0)continue;ssize_t n=read(fd,b,sizeof b-1);close(fd);if(n<=0)continue;b[n]=0;for(ssize_t i=0;i<n;i++)if(!b[i])b[i]=' ';printf("%s %s\n",e->d_name,b);}closedir(d);return 0;}
