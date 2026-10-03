#pragma once
#ifdef NDEBUG
#define assert(x) ((void)0)
#else
void __blockos_assert_fail(const char *expr, const char *file, int line, const char *func);
#define assert(x) ((x) ? (void)0 : __blockos_assert_fail(#x, __FILE__, __LINE__, __func__))
#endif
