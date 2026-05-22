#pragma once

typedef unsigned char       uint8_t;
typedef unsigned short      uint16_t;
typedef unsigned int        uint32_t;
typedef unsigned long long  uint64_t;
typedef signed char         int8_t;
typedef signed short        int16_t;
typedef signed int          int32_t;
typedef signed long long    int64_t;

typedef uint64_t    uintptr_t;
typedef int64_t     intptr_t;
typedef uint64_t    size_t;
typedef int64_t     ssize_t;
typedef uint8_t     bool;

#define true    1
#define false   0
#define NULL    ((void*)0)

#define PACKED          __attribute__((packed))
#define NORETURN        __attribute__((noreturn))
#define UNUSED          __attribute__((unused))
#define ALIGNED(n)      __attribute__((aligned(n)))
#define LIKELY(x)       __builtin_expect(!!(x), 1)
#define UNLIKELY(x)     __builtin_expect(!!(x), 0)

#define PAGE_SIZE       4096ULL
#define PAGE_SHIFT      12

#define ALIGN_UP(x, a)      (((uint64_t)(x) + (a) - 1) & ~((a) - 1))
#define ALIGN_DOWN(x, a)    ((uint64_t)(x) & ~((a) - 1))

#define MIN(a,b)    ((a) < (b) ? (a) : (b))
#define MAX(a,b)    ((a) > (b) ? (a) : (b))

#define ARRAY_SIZE(x)   (sizeof(x) / sizeof((x)[0]))

/* Compile-time assert */
#define STATIC_ASSERT(cond, msg)    _Static_assert(cond, msg)

#define UINT64_MAX  0xFFFFFFFFFFFFFFFFULL
#define INT64_MAX   0x7FFFFFFFFFFFFFFFLL
#define INT64_MIN   (-INT64_MAX - 1LL)
#define SIZE_MAX    UINT64_MAX
