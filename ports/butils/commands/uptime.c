#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
int main(void){int fd=open("/proc/uptime",O_RDONLY);if(fd<0){write(2,"uptime: /proc/uptime unavailable\n",33);return 1;}char b[128];ssize_t n=read(fd,b,sizeof b-1);close(fd);if(n<=0)return 1;b[n]=0;long sec=strtol(b,0,10);printf("up %ld days, %ld:%ld\n",sec/86400,(sec/3600)%24,(sec/60)%60);return 0;}
