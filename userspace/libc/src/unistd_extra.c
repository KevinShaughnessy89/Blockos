#include "unistd.h"
#include "blockos_syscall.h"
#include "errno.h"
#include "stdlib.h"
#include "string.h"

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


int execv(const char* path, char* const argv[]) {
    return execve(path, argv, environ);
}

int execvp(const char* file, char* const argv[]) {
    if (!file || !*file) { errno = 2; return -1; }
    if (file[0] == '/') return execv(file, argv);
    const char* path = getenv("PATH");
    if (!path || !*path) path = "/System/bin:/bin";
    char candidate[512];
    const char* p = path;
    while (1) {
        const char* q = p;
        while (*q && *q != ':') ++q;
        size_t dlen = (size_t)(q - p);
        size_t flen = strlen(file);
        if (dlen + 1 + flen + 1 < sizeof(candidate)) {
            if (dlen == 0) {
                candidate[0]='.';
                candidate[1]='/';
                memcpy(candidate+2, file, flen+1);
            } else {
                memcpy(candidate, p, dlen);
                candidate[dlen]='/';
                memcpy(candidate+dlen+1, file, flen+1);
            }
            execv(candidate, argv);
            if (errno != 2 && errno != 13) return -1;
        }
        if (!*q) break;
        p = q + 1;
    }
    errno = 2;
    return -1;
}

pid_t fork(void) {
    long r = __blockos_syscall(__SYS_fork, 0, 0, 0, 0, 0, 0);
    if (r < 0) { errno = (int)-r; return -1; }
    return (pid_t)r;
}

int setpgid(pid_t pid, pid_t pgid) {
    long r = __blockos_syscall(__SYS_setpgid, pid, pgid, 0, 0, 0, 0);
    if (r < 0) { errno=(int)-r; return -1; } return 0;
}
pid_t getpgid(pid_t pid) {
    long r = __blockos_syscall(__SYS_getpgid, pid, 0, 0, 0, 0, 0);
    if (r < 0) { errno=(int)-r; return -1; } return (pid_t)r;
}
pid_t getpgrp(void) { return getpgid(0); }
pid_t setsid(void) {
    long r = __blockos_syscall(__SYS_setsid, 0, 0, 0, 0, 0, 0);
    if (r < 0) { errno=(int)-r; return -1; } return (pid_t)r;
}

ssize_t readlink(const char* path, char* buf, size_t size) {
    long r = __blockos_syscall(__SYS_readlink, (long)path, (long)buf, (long)size, 0, 0, 0);
    if (r < 0) { errno=(int)-r; return -1; } return (ssize_t)r;
}
ssize_t readlinkat(int dirfd, const char* path, char* buf, size_t size) {
    (void)dirfd;
    return readlink(path, buf, size);
}
