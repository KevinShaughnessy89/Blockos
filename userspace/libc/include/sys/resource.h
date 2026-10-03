#pragma once
#include <stdint.h>
#include <sys/types.h>
#include <sys/time.h>
struct rusage { struct timeval ru_utime; struct timeval ru_stime; long ru_maxrss; };
#define RUSAGE_SELF 0
#define RUSAGE_CHILDREN (-1)
int getrusage(int who, struct rusage *usage);
