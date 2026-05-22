#pragma once
#include "types.h"

typedef struct {
    volatile uint32_t locked;
} spinlock_t;

#define SPINLOCK_INIT   { .locked = 0 }

static inline void spin_lock(spinlock_t *l)
{
    while (__sync_lock_test_and_set(&l->locked, 1))
        while (l->locked) __asm__ __volatile__("pause");
}

static inline void spin_unlock(spinlock_t *l)
{
    __sync_lock_release(&l->locked);
}

static inline int spin_trylock(spinlock_t *l)
{
    return !__sync_lock_test_and_set(&l->locked, 1);
}

/* IRQ-safe versions: save/restore interrupt flag */
static inline uint64_t irq_save(void)
{
    uint64_t flags;
    __asm__ __volatile__("pushfq; popq %0; cli" : "=r"(flags) :: "memory");
    return flags;
}

static inline void irq_restore(uint64_t flags)
{
    __asm__ __volatile__("pushq %0; popfq" :: "r"(flags) : "memory", "cc");
}
