#pragma once
#include <stdint.h>

/*
 * This layout is BlockOS-specific, not the real glibc x86-64 struct stat
 * ABI - it matches exactly what kernel/user_syscall.cpp's SYS_fstat
 * handler currently writes (q[0]=dev, q[1]=ino, q[2]=mode, q[7]=size,
 * 128 bytes total, everything else zeroed). If you ever change what the
 * kernel writes, update this struct to match, or fstat()'s callers will
 * silently read garbage/zero for whatever field moved.
 */
struct stat {
    unsigned long st_dev;
    unsigned long st_ino;
    unsigned long st_mode;
    unsigned long __pad3;
    unsigned long __pad4;
    unsigned long __pad5;
    unsigned long __pad6;
    unsigned long st_size;
    unsigned long __pad8;
    unsigned long __pad9;
    unsigned long __pad10;
    unsigned long __pad11;
    unsigned long __pad12;
    unsigned long __pad13;
    unsigned long __pad14;
    unsigned long __pad15;
};

#define S_IFMT   0170000
#define S_IFREG  0100000
#define S_IFDIR  0040000
#define S_IFCHR  0020000
#define S_IFBLK  0060000
#define S_IFIFO  0010000
#define S_IFSOCK 0140000
#define S_IRUSR  0400
#define S_IWUSR  0200
#define S_IXUSR  0100
#define S_IRGRP  0040
#define S_IWGRP  0020
#define S_IXGRP  0010
#define S_IROTH  0004
#define S_IWOTH  0002
#define S_IXOTH  0001

#define AT_FDCWD (-100)
#define AT_SYMLINK_NOFOLLOW 0x100

int fstat(int fd, struct stat* buf);
int stat(const char* path, struct stat* buf);
int lstat(const char* path, struct stat* buf);
int fstatat(int dirfd, const char* path, struct stat* buf, int flags);
