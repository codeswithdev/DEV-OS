#pragma once
#include "../../include/types.h"

#define MSR_EFER        0xC0000080
#define MSR_STAR        0xC0000081
#define MSR_LSTAR       0xC0000082
#define MSR_CSTAR       0xC0000083
#define MSR_SFMASK      0xC0000084
#define MSR_FS_BASE     0xC0000100
#define MSR_GS_BASE     0xC0000101
#define MSR_KERNEL_GS   0xC0000102

#define EFER_SCE        (1ULL << 0)
#define EFER_LME        (1ULL << 8)
#define EFER_LMA        (1ULL << 10)
#define EFER_NXE        (1ULL << 11)

static inline uint64_t rdmsr(uint32_t msr)
{
    uint32_t lo, hi;
    __asm__ __volatile__("rdmsr" : "=a"(lo), "=d"(hi) : "c"(msr));
    return ((uint64_t)hi << 32) | lo;
}

static inline void wrmsr(uint32_t msr, uint64_t val)
{
    __asm__ __volatile__("wrmsr"
        :: "c"(msr), "a"((uint32_t)val), "d"((uint32_t)(val >> 32)));
}

static inline void cpu_enable_nxe(void)
{
    uint64_t efer = rdmsr(MSR_EFER);
    efer |= EFER_NXE;
    wrmsr(MSR_EFER, efer);
}

static inline void cpu_halt(void) {
    __asm__ __volatile__("hlt");
}

static inline void cpu_disable_interrupts(void) {
    __asm__ __volatile__("cli" ::: "memory");
}

static inline void cpu_enable_interrupts(void) {
    __asm__ __volatile__("sti" ::: "memory");
}

static inline uint64_t cpu_read_rflags(void) {
    uint64_t f;
    __asm__ __volatile__("pushfq; popq %0" : "=r"(f));
    return f;
}
