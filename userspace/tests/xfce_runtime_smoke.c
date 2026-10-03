#include "stdio.h"
#include "stdlib.h"
#include "unistd.h"
#include "pthread.h"
#include "sys/utsname.h"
#include "sys/stat.h"
#include "sys/time.h"
#include "sys/socket.h"
#include "sys/select.h"
#include "fcntl.h"
#include "errno.h"
#include "string.h"

static void* thread_main(void*arg){return arg;}

int main(void){
    struct utsname u;
    if(uname(&u)!=0){printf("xfce-smoke: uname failed errno=%d\n",errno);return 1;}
    struct timespec ts;
    if(clock_gettime(CLOCK_MONOTONIC,&ts)!=0){printf("xfce-smoke: clock failed errno=%d\n",errno);return 1;}
    pthread_t t; int value=42; void*ret=0;
    if(pthread_create(&t,0,thread_main,&value)!=0||pthread_join(t,&ret)!=0||ret!=&value){printf("xfce-smoke: pthread failed\n");return 1;}
    int sv[2];
    if(socketpair(AF_UNIX,SOCK_STREAM,0,sv)!=0){printf("xfce-smoke: socketpair failed errno=%d\n",errno);return 1;}
    const char msg[]="ok"; char buf[8]={0};
    if(write(sv[0],msg,2)!=2||read(sv[1],buf,2)!=2||strcmp(buf,"ok")!=0){printf("xfce-smoke: unix socket I/O failed errno=%d\n",errno);return 1;}
    close(sv[0]); close(sv[1]);
    printf("xfce-smoke: BlockOS runtime ABI ready (%s %s)\n",u.sysname,u.machine);
    return 0;
}
