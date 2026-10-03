#pragma once
#include <stddef.h>
#include <sys/time.h>

#define FD_SETSIZE 1024
typedef struct { unsigned long bits[(FD_SETSIZE + (8*sizeof(unsigned long)-1))/(8*sizeof(unsigned long))]; } fd_set;
#define __FD_WORD(fd) ((unsigned)(fd) / (8u*sizeof(unsigned long)))
#define __FD_BIT(fd)  ((unsigned)(fd) % (8u*sizeof(unsigned long)))
#define FD_ZERO(s) do { for (size_t __i=0; __i<sizeof((s)->bits)/sizeof((s)->bits[0]); ++__i) (s)->bits[__i]=0; } while (0)
#define FD_SET(fd,s) do { if ((fd)>=0 && (fd)<FD_SETSIZE) (s)->bits[__FD_WORD(fd)] |= (1UL<<__FD_BIT(fd)); } while (0)
#define FD_CLR(fd,s) do { if ((fd)>=0 && (fd)<FD_SETSIZE) (s)->bits[__FD_WORD(fd)] &= ~(1UL<<__FD_BIT(fd)); } while (0)
#define FD_ISSET(fd,s) (((fd)>=0 && (fd)<FD_SETSIZE) ? (((s)->bits[__FD_WORD(fd)]>>( __FD_BIT(fd)))&1UL) : 0)
int select(int nfds, fd_set *readfds, fd_set *writefds, fd_set *exceptfds, struct timeval *timeout);
