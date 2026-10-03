
#include "../include/stdlib.h"
#include "../include/string.h"

#include "allocator.hpp"

#include <stddef.h>
#include <stdint.h>
#include <limits.h>


namespace
{

struct alignas(max_align_t) AllocationHeader
{
    uint64_t magic;
    size_t size;
};


constexpr uint64_t ALLOCATION_MAGIC =
    0x424C4B4F534D454DULL;


static uint32_t random_state = 1;


static AllocationHeader* header_from_ptr(void* ptr)
{
    if (!ptr)
        return nullptr;

    auto* raw =
        static_cast<unsigned char*>(ptr);

    return reinterpret_cast<AllocationHeader*>(
        raw - sizeof(AllocationHeader));
}


static bool valid_header(AllocationHeader* h)
{
    return h &&
           h->magic == ALLOCATION_MAGIC;
}


static void swap_bytes(
    unsigned char* a,
    unsigned char* b,
    size_t n)
{
    for (size_t i = 0; i < n; ++i)
    {
        unsigned char t = a[i];
        a[i] = b[i];
        b[i] = t;
    }
}


static void qsort_impl(
    unsigned char* base,
    size_t left,
    size_t right,
    size_t element_size,
    int (*compare)(const void*, const void*))
{
    if (left >= right)
        return;

    size_t i = left;
    size_t j = right;

    size_t pivot_index =
        left + (right - left) / 2;

    unsigned char* pivot =
        static_cast<unsigned char*>(
            ::malloc(element_size));

    if (!pivot)
        return;

    memcpy(
        pivot,
        base + pivot_index * element_size,
        element_size);

    while (i <= j)
    {
        while (
            compare(
                base + i * element_size,
                pivot) < 0)
        {
            ++i;

            if (i > right)
                break;
        }

        while (
            compare(
                base + j * element_size,
                pivot) > 0)
        {
            if (j == 0)
                break;

            --j;
        }

        if (i <= j)
        {
            swap_bytes(
                base + i * element_size,
                base + j * element_size,
                element_size);

            ++i;

            if (j == 0)
                break;

            --j;
        }
    }

    ::free(pivot);

    if (left < j)
    {
        qsort_impl(
            base,
            left,
            j,
            element_size,
            compare);
    }

    if (i < right)
    {
        qsort_impl(
            base,
            i,
            right,
            element_size,
            compare);
    }
}

}


extern "C"
{

void* malloc(size_t size)
{
    if (size == 0)
        size = 1;

    if (size >
        SIZE_MAX - sizeof(AllocationHeader))
    {
        return nullptr;
    }

    const size_t total =
        sizeof(AllocationHeader) + size;

    auto* h =
        static_cast<AllocationHeader*>(
            allocator::alloc(
                total,
                alignof(AllocationHeader)));

    if (!h)
        return nullptr;

    h->magic = ALLOCATION_MAGIC;
    h->size = size;

    return reinterpret_cast<unsigned char*>(h)
           + sizeof(AllocationHeader);
}


void free(void* ptr)
{
    if (!ptr)
        return;

    AllocationHeader* h =
        header_from_ptr(ptr);

    if (!valid_header(h))
        return;

    h->magic = 0;

    allocator::free(h);
}


void* calloc(size_t count, size_t size)
{
    if (count != 0 &&
        size > SIZE_MAX / count)
    {
        return nullptr;
    }

    size_t total = count * size;

    void* ptr = malloc(total);

    if (!ptr)
        return nullptr;

    memset(ptr, 0, total);

    return ptr;
}


void* realloc(void* ptr, size_t new_size)
{
    if (!ptr)
        return malloc(new_size);

    if (new_size == 0)
    {
        free(ptr);
        return nullptr;
    }

    AllocationHeader* old_header =
        header_from_ptr(ptr);

    if (!valid_header(old_header))
        return nullptr;

    size_t old_size = old_header->size;

    if (new_size <= old_size)
    {
        old_header->size = new_size;
        return ptr;
    }

    void* new_ptr =
        malloc(new_size);

    if (!new_ptr)
        return nullptr;

    memcpy(
        new_ptr,
        ptr,
        old_size);

    free(ptr);

    return new_ptr;
}


int atoi(const char* s)
{
    if (!s)
        return 0;

    while (*s == ' ' ||
           *s == '\t' ||
           *s == '\n' ||
           *s == '\r' ||
           *s == '\f' ||
           *s == '\v')
    {
        ++s;
    }

    int sign = 1;

    if (*s == '-')
    {
        sign = -1;
        ++s;
    }
    else if (*s == '+')
    {
        ++s;
    }

    int result = 0;

    while (*s >= '0' && *s <= '9')
    {
        result = result * 10 + (*s - '0');
        ++s;
    }

    return result * sign;
}


long atol(const char* s)
{
    if (!s)
        return 0;

    while (*s == ' ' ||
           *s == '\t' ||
           *s == '\n' ||
           *s == '\r' ||
           *s == '\f' ||
           *s == '\v')
    {
        ++s;
    }

    long sign = 1;

    if (*s == '-')
    {
        sign = -1;
        ++s;
    }
    else if (*s == '+')
    {
        ++s;
    }

    long result = 0;

    while (*s >= '0' && *s <= '9')
    {
        result = result * 10 + (*s - '0');
        ++s;
    }

    return result * sign;
}


unsigned long strtoul(
    const char* s,
    char** endptr,
    int base)
{
    const char* start = s;

    if (endptr)
        *endptr = (char*) start;

    if (!s)
        return 0;

    if (base < 0 || base == 1 || base > 36)
        return 0;

    while (*s == ' ' ||
           *s == '\t' ||
           *s == '\n' ||
           *s == '\r' ||
           *s == '\f' ||
           *s == '\v')
    {
        ++s;
    }

    bool negate = false;

    if (*s == '-')
    {
        negate = true;
        ++s;
    }
    else if (*s == '+')
    {
        ++s;
    }

    /*
     * Compute ULONG_MAX without depending on the host
     * <limits.h>. This is important for the BlockOS libc
     * cross-build environment.
     */
    const unsigned long ulong_max =
        static_cast<unsigned long>(~0UL);

    if (base == 0)
    {
        if (s[0] == '0')
        {
            if (s[1] == 'x' || s[1] == 'X')
            {
                /*
                 * Only consume 0x/0X as a hexadecimal prefix
                 * when it is actually followed by a valid hex digit.
                 */
                const char next = s[2];

                bool valid_hex =
                    (next >= '0' && next <= '9') ||
                    (next >= 'a' && next <= 'f') ||
                    (next >= 'A' && next <= 'F');

                if (valid_hex)
                {
                    base = 16;
                    s += 2;
                }
                else
                {
                    base = 8;
                }
            }
            else
            {
                base = 8;
            }
        }
        else
        {
            base = 10;
        }
    }
    else if (base == 16)
    {
        if (s[0] == '0' &&
            (s[1] == 'x' || s[1] == 'X'))
        {
            const char next = s[2];

            bool valid_hex =
                (next >= '0' && next <= '9') ||
                (next >= 'a' && next <= 'f') ||
                (next >= 'A' && next <= 'F');

            if (valid_hex)
                s += 2;
        }
    }

    unsigned long result = 0;
    bool any = false;
    bool overflow = false;

    const unsigned long ub =
        static_cast<unsigned long>(base);

    const unsigned long limit =
        ulong_max / ub;

    const unsigned long remainder =
        ulong_max % ub;

    for (;; ++s)
    {
        unsigned long digit;

        if (*s >= '0' && *s <= '9')
        {
            digit =
                static_cast<unsigned long>(
                    *s - '0');
        }
        else if (*s >= 'a' && *s <= 'z')
        {
            digit =
                static_cast<unsigned long>(
                    *s - 'a') + 10UL;
        }
        else if (*s >= 'A' && *s <= 'Z')
        {
            digit =
                static_cast<unsigned long>(
                    *s - 'A') + 10UL;
        }
        else
        {
            break;
        }

        if (digit >= ub)
            break;

        /*
         * Check overflow BEFORE performing:
         *
         *   result * base + digit
         *
         * so unsigned wraparound cannot hide the overflow.
         */
        if (!overflow)
        {
            if (result > limit ||
                (result == limit &&
                 digit > remainder))
            {
                overflow = true;
            }
            else
            {
                result =
                    result * ub + digit;
            }
        }

        any = true;
    }

    /*
     * No digits converted:
     * endptr remains at the original string.
     */
    if (!any)
        return 0;

    if (endptr)
        *endptr = (char*) s;

    if (overflow)
        return ulong_max;

    if (negate)
        return static_cast<unsigned long>(
            0UL - result);

    return result;
}


int abs(int x)
{
    return x < 0 ? -x : x;
}


long labs(long x)
{
    return x < 0 ? -x : x;
}


void srand(unsigned int seed)
{
    random_state =
        seed ? seed : 1;
}


int rand(void)
{
    random_state =
        random_state * 1664525u +
        1013904223u;

    return static_cast<int>(
        (random_state >> 1) &
        0x7fffffffU);
}

}


Ha a következő hibánál megáll, küldd be azt a hibát, és ugyanígy javítjuk tovább.
