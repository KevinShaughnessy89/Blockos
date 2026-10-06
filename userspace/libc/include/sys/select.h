#pragma once
#include <stddef.h>
#include <sys/time.h>

#define FD_SETSIZE 1024
#define __NFDBITS (8 * sizeof(unsigned long))
typedef struct {
    unsigned long fds_bits[(FD_SETSIZE + __NFDBITS - 1) / __NFDBITS];
} fd_set;

#define FD_ZERO(p) do { \
    size_t __i; \
    for (__i = 0; __i < sizeof((p)->fds_bits) / sizeof((p)->fds_bits[0]); ++__i) \
        (p)->fds_bits[__i] = 0; \
} while (0)
#define FD_SET(fd,p) ((p)->fds_bits[(fd)/__NFDBITS] |= (1UL << ((fd)%__NFDBITS)))
#define FD_CLR(fd,p) ((p)->fds_bits[(fd)/__NFDBITS] &= ~(1UL << ((fd)%__NFDBITS)))
#define FD_ISSET(fd,p) (((p)->fds_bits[(fd)/__NFDBITS] >> ((fd)%__NFDBITS)) & 1UL)

int select(int nfds, fd_set *readfds, fd_set *writefds,
           fd_set *exceptfds, struct timeval *timeout);
