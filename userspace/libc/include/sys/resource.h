#pragma once
#include <stdint.h>
struct rlimit { uint64_t rlim_cur, rlim_max; };
#define RLIM_INFINITY (~0ULL)
#define RLIMIT_CPU 0
#define RLIMIT_FSIZE 1
#define RLIMIT_DATA 2
#define RLIMIT_STACK 3
#define RLIMIT_CORE 4
#define RLIMIT_AS 9
int getrlimit(int, struct rlimit*);
int setrlimit(int, const struct rlimit*);
