#pragma once
#include <stdint.h>
struct timeval { int64_t tv_sec; int64_t tv_usec; };
struct timezone { int tz_minuteswest; int tz_dsttime; };
struct timespec { int64_t tv_sec; long tv_nsec; };
#define CLOCK_REALTIME 0
#define CLOCK_MONOTONIC 1
#define CLOCK_PROCESS_CPUTIME_ID 2
#define CLOCK_THREAD_CPUTIME_ID 3
int gettimeofday(struct timeval*, struct timezone*);
int clock_gettime(int clock_id, struct timespec* ts);
int nanosleep(const struct timespec* req, struct timespec* rem);
