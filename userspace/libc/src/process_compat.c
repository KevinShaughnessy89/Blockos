#include "unistd.h"
#include "blockos_syscall.h"
#include "errno.h"
#include "signal.h"
#include "stdlib.h"
#include <string.h>

pid_t fork(void) { errno = 38; return -1; }
int setsid(void) { pid_t p=getpid(); return p>0?(int)p:-1; }
int setpgid(pid_t pid,pid_t pgid){(void)pid;(void)pgid;return 0;}
pid_t getpgrp(void){return getpid();}
pid_t getpgid(pid_t pid){return pid>0?pid:getpid();}
int tcsetpgrp(int fd,pid_t pgrp){(void)fd;(void)pgrp;return 0;}
unsigned alarm(unsigned seconds){(void)seconds;return 0;}
int killpg(pid_t pgrp,int sig){return kill(pgrp,sig);}

static int exec_path(const char *file,char *const argv[]){
    char candidate[256]; const char *path=getenv("PATH"); const char *p;
    if(!file||!*file){errno=22;return -1;}
    if(strchr(file,'/')) return execve(file,argv,0);
    if(!path||!*path)path="/system/bin:/bin";
    p=path;
    while(*p){const char*start=p;size_t len;while(*p&&*p!=':')++p;len=(size_t)(p-start);
      if(len && len+1+strlen(file)+1<sizeof(candidate)){memcpy(candidate,start,len);candidate[len]='/';strcpy(candidate+len+1,file);execve(candidate,argv,0);}
      if(*p==':')++p;
    }
    errno=2;return -1;
}
int execv(const char*path,char*const argv[]){return execve(path,argv,0);}
int execvp(const char*file,char*const argv[]){return exec_path(file,argv);}
