#include "unistd.h"
#include "sys/time.h"
#include "blockos_syscall.h"
#include "errno.h"

int execve(const char* path,char* const argv[],char* const envp[]){
    long r=__blockos_syscall(__SYS_execve,(long)path,(long)argv,(long)envp,0,0,0);
    if(r<0){errno=(int)-r;return -1;}
    return (int)r;
}
pid_t getppid(void){return (pid_t)__blockos_syscall(__SYS_getppid,0,0,0,0,0,0);}
int chdir(const char* path){long r=__blockos_syscall(__SYS_chdir,(long)path,0,0,0,0,0);if(r<0){errno=(int)-r;return -1;}return 0;}
char* getcwd(char* buf,size_t size){long r=__blockos_syscall(__SYS_getcwd,(long)buf,(long)size,0,0,0,0);if(r<0){errno=(int)-r;return 0;}return buf;}
int pipe2(int pipefd[2],int flags){long r=__blockos_syscall(__SYS_pipe2,(long)pipefd,flags,0,0,0,0);if(r<0){errno=(int)-r;return -1;}return 0;}
long syscall(long n,long a0,long a1,long a2,long a3,long a4,long a5){return __blockos_syscall(n,a0,a1,a2,a3,a4,a5);}

int sched_yield(void){long r=__blockos_syscall(__SYS_sched_yield,0,0,0,0,0,0);if(r<0){errno=(int)-r;return -1;}return 0;}
int nanosleep(const struct timespec* req,struct timespec* rem){long r=__blockos_syscall(__SYS_nanosleep,(long)req,(long)rem,0,0,0,0);if(r<0){errno=(int)-r;return -1;}return 0;}

int unlink(const char* path){long r=__blockos_syscall(__SYS_unlink,(long)path,0,0,0,0,0);if(r<0){errno=(int)-r;return -1;}return 0;}
int rename(const char* a,const char* b){long r=__blockos_syscall(__SYS_rename,(long)a,(long)b,0,0,0,0);if(r<0){errno=(int)-r;return -1;}return 0;}
int mkdir(const char* path,unsigned mode){long r=__blockos_syscall(__SYS_mkdir,(long)path,mode,0,0,0,0);if(r<0){errno=(int)-r;return -1;}return 0;}

#include "sys/socket.h"
#include "errno.h"
ssize_t readv(int fd,const struct iovec*iov,int iovcnt){if(!iov||iovcnt<0){errno=22;return -1;}ssize_t total=0;for(int i=0;i<iovcnt;i++){if(iov[i].iov_len==0)continue;ssize_t n=read(fd,iov[i].iov_base,iov[i].iov_len);if(n<0)return total?total:-1;total+=n;if((size_t)n<iov[i].iov_len)break;}return total;}
ssize_t writev(int fd,const struct iovec*iov,int iovcnt){if(!iov||iovcnt<0){errno=22;return -1;}ssize_t total=0;for(int i=0;i<iovcnt;i++){if(iov[i].iov_len==0)continue;ssize_t n=write(fd,iov[i].iov_base,iov[i].iov_len);if(n<0)return total?total:-1;total+=n;if((size_t)n<iov[i].iov_len)break;}return total;}
pid_t fork(void){uintptr_t sp;__asm__ volatile("mov %%rsp,%0":"=r"(sp));long r=__blockos_syscall(__SYS_clone,0,(long)sp,0,0,0,0);if(r<0){errno=(int)-r;return -1;}return (pid_t)r;}
pid_t vfork(void){return fork();}
int setsid(void){return (int)getpid();}
int setpgid(pid_t pid,pid_t pgid){(void)pid;(void)pgid;return 0;}
pid_t getpgrp(void){return getpid();}
pid_t tcgetpgrp(int fd){(void)fd;return getpgrp();}
int tcsetpgrp(int fd,pid_t pgrp){(void)fd;(void)pgrp;return 0;}
