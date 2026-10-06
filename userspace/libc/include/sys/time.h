#pragma once
#include <stdint.h>
struct timeval { int64_t tv_sec; int64_t tv_usec; };
int gettimeofday(struct timeval*, void*);
