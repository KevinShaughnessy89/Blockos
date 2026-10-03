#pragma once
#include <stddef.h>
int strcasecmp(const char *, const char *);
int strncasecmp(const char *, const char *, size_t);
void *memmem(const void *, size_t, const void *, size_t);
int ffs(int);
