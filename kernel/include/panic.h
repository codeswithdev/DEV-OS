#pragma once
#include "types.h"
#include "../arch/x86_64/serial.h"

NORETURN static inline void panic_halt(void)
{
    __asm__ __volatile__("cli");
    for (;;) __asm__ __volatile__("hlt");
}

#define PANIC(fmt, ...) do {                                        \
    serial_printf("\n[PANIC] " fmt "\n", ##__VA_ARGS__);            \
    serial_printf("  at %s:%d\n", __FILE__, __LINE__);              \
    panic_halt();                                                   \
} while (0)

#define ASSERT(cond) do {                                           \
    if (UNLIKELY(!(cond))) {                                        \
        PANIC("Assertion failed: " #cond);                          \
    }                                                               \
} while (0)

#define BUG_ON(cond) do {                                           \
    if (UNLIKELY(cond)) {                                           \
        PANIC("BUG: " #cond);                                       \
    }                                                               \
} while (0)
