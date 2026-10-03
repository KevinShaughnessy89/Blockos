#pragma once
#include <stddef.h>
#include <stdint.h>
#include <sys/types.h>
#include <sys/time.h>
struct iovec;

ssize_t read(int fd, void* buf, size_t count);
ssize_t write(int fd, const void* buf, size_t count);
int close(int fd);
off_t lseek(int fd, off_t offset, int whence);
pid_t getpid(void);
pid_t getppid(void);
uid_t getuid(void); gid_t getgid(void); uid_t geteuid(void); gid_t getegid(void);
void* sbrk(long increment);
__attribute__((noreturn)) void _exit(int code);

#define SEEK_SET 0
#define SEEK_CUR 1
#define SEEK_END 2
#define F_OK 0
#define X_OK 1
#define W_OK 2
#define R_OK 4

int open(const char* path, int flags, ...);
int openat(int dirfd, const char* path, int flags, ...);
int execve(const char*, char* const[], char* const[]);
int sched_yield(void);
int nanosleep(const struct timespec*, struct timespec*);
unsigned sleep(unsigned);
int usleep(unsigned);
int unlink(const char* path);
int rename(const char* oldpath, const char* newpath);
int mkdir(const char* path, unsigned mode);
int rmdir(const char* path);
int dup(int fd);
int dup2(int oldfd, int newfd);
int isatty(int fd);
ssize_t readv(int fd, const struct iovec* iov, int iovcnt);
ssize_t writev(int fd, const struct iovec* iov, int iovcnt);
int pipe2(int pipefd[2], int flags);
pid_t fork(void); pid_t vfork(void); int setsid(void); int setpgid(pid_t,pid_t); pid_t getpgrp(void); pid_t tcgetpgrp(int); int tcsetpgrp(int,pid_t);
long syscall(long n,long,long,long,long,long,long);
char* getcwd(char*,size_t);
int chdir(const char*);
int access(const char*,int);
int rmdir(const char*); int ftruncate(int,off_t); int fsync(int); ssize_t readlink(const char*,char*,size_t);
char* realpath(const char*,char*);
int gethostname(char*,size_t);
long sysconf(int name);
long getpagesize(void);

#define _SC_PAGESIZE 2
#define _SC_OPEN_MAX 4
#define _SC_NPROCESSORS_CONF 85
#define _SC_NPROCESSORS_ONLN 84
