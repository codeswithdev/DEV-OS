#pragma once
#include "../include/types.h"

#define VMM_PRESENT     (1ULL << 0)
#define VMM_WRITABLE    (1ULL << 1)
#define VMM_USER        (1ULL << 2)
#define VMM_PWT         (1ULL << 3)
#define VMM_PCD         (1ULL << 4)
#define VMM_ACCESSED    (1ULL << 5)
#define VMM_DIRTY       (1ULL << 6)
#define VMM_HUGE        (1ULL << 7)
#define VMM_GLOBAL      (1ULL << 8)
#define VMM_NO_EXEC     (1ULL << 63)

#define VMM_KERNEL_RO   (VMM_PRESENT | VMM_GLOBAL)
#define VMM_KERNEL_RW   (VMM_PRESENT | VMM_WRITABLE | VMM_GLOBAL)
#define VMM_USER_RO     (VMM_PRESENT | VMM_USER)
#define VMM_USER_RW     (VMM_PRESENT | VMM_WRITABLE | VMM_USER)
#define VMM_MMIO        (VMM_PRESENT | VMM_WRITABLE | VMM_PCD | VMM_PWT | VMM_GLOBAL)

#define VMM_PHYS_MASK   0x000FFFFFFFFFF000ULL

#define KERNEL_VIRT_BASE    0xFFFFFFFF80000000ULL
#define KERNEL_PHYS_OFFSET  0x0000000000100000ULL
#define USER_SPACE_MAX      0x00007FFFFFFFFFFFULL

#define PML4_IDX(v)     (((v) >> 39) & 0x1FF)
#define PDPT_IDX(v)     (((v) >> 30) & 0x1FF)
#define PD_IDX(v)       (((v) >> 21) & 0x1FF)
#define PT_IDX(v)       (((v) >> 12) & 0x1FF)

typedef uint64_t *pml4_t;

/* Global kernel PML4 — exported for vmm_ext.c and heap */
extern pml4_t kernel_pml4;

void    vmm_init(void);
pml4_t  vmm_create_address_space(void);
void    vmm_map(pml4_t pml4, uint64_t virt, uint64_t phys, uint64_t flags);
void    vmm_map_range(pml4_t pml4, uint64_t virt, uint64_t phys,
                      uint64_t size, uint64_t flags);
void    vmm_unmap(pml4_t pml4, uint64_t virt);
uint64_t vmm_translate(pml4_t pml4, uint64_t virt);
void    vmm_switch(pml4_t pml4);

static inline void vmm_invlpg(uint64_t virt) {
    __asm__ __volatile__("invlpg (%0)" :: "r"(virt) : "memory");
}
static inline void vmm_flush_tlb(void) {
    uint64_t cr3;
    __asm__ __volatile__(
        "mov %%cr3, %0\n\t"
        "mov %0, %%cr3\n\t"
        : "=r"(cr3) :: "memory"
    );
}
