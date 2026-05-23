#include "vmm.h"
#include "pmm.h"
#include "../arch/x86_64/serial.h"
#include "../arch/x86_64/cpu.h"
#include "../lib/string.h"

pml4_t kernel_pml4 = NULL;

static inline uint64_t *phys_to_pt(uint64_t phys)
{
    return (uint64_t *)(uintptr_t)phys;
}

static uint64_t *get_or_create(uint64_t *table, uint16_t idx, uint64_t flags)
{
    if (!(table[idx] & VMM_PRESENT)) {
        void *page = pmm_alloc();
        if (!page) return NULL;
        table[idx] = (uint64_t)(uintptr_t)page | flags;
    }
    return phys_to_pt(table[idx] & VMM_PHYS_MASK);
}

void vmm_map(pml4_t pml4, uint64_t virt, uint64_t phys, uint64_t flags)
{
    uint16_t p4 = PML4_IDX(virt);
    uint16_t p3 = PDPT_IDX(virt);
    uint16_t p2 = PD_IDX(virt);
    uint16_t p1 = PT_IDX(virt);

    uint64_t mid_flags = VMM_PRESENT | VMM_WRITABLE | (flags & VMM_USER);

    uint64_t *pdpt = get_or_create(pml4,  p4, mid_flags);
    if (!pdpt) return;
    uint64_t *pd   = get_or_create(pdpt,  p3, mid_flags);
    if (!pd) return;
    uint64_t *pt   = get_or_create(pd,    p2, mid_flags);
    if (!pt) return;

    pt[p1] = (phys & VMM_PHYS_MASK) | flags;
    vmm_invlpg(virt);
}

void vmm_map_range(pml4_t pml4, uint64_t virt, uint64_t phys,
                   uint64_t size, uint64_t flags)
{
    uint64_t pages = ALIGN_UP(size, PAGE_SIZE) / PAGE_SIZE;
    for (uint64_t i = 0; i < pages; i++)
        vmm_map(pml4, virt + i * PAGE_SIZE, phys + i * PAGE_SIZE, flags);
}

void vmm_unmap(pml4_t pml4, uint64_t virt)
{
    uint16_t p4 = PML4_IDX(virt);
    uint16_t p3 = PDPT_IDX(virt);
    uint16_t p2 = PD_IDX(virt);
    uint16_t p1 = PT_IDX(virt);

    if (!(pml4[p4] & VMM_PRESENT)) return;
    uint64_t *pdpt = phys_to_pt(pml4[p4] & VMM_PHYS_MASK);
    if (!(pdpt[p3] & VMM_PRESENT)) return;
    uint64_t *pd   = phys_to_pt(pdpt[p3] & VMM_PHYS_MASK);
    if (!(pd[p2] & VMM_PRESENT)) return;
    uint64_t *pt   = phys_to_pt(pd[p2] & VMM_PHYS_MASK);

    pt[p1] = 0;
    vmm_invlpg(virt);
}

uint64_t vmm_translate(pml4_t pml4, uint64_t virt)
{
    uint16_t p4 = PML4_IDX(virt);
    uint16_t p3 = PDPT_IDX(virt);
    uint16_t p2 = PD_IDX(virt);
    uint16_t p1 = PT_IDX(virt);

    if (!(pml4[p4] & VMM_PRESENT)) return 0;
    uint64_t *pdpt = phys_to_pt(pml4[p4] & VMM_PHYS_MASK);
    if (!(pdpt[p3] & VMM_PRESENT)) return 0;
    if (pdpt[p3] & VMM_HUGE)
        return (pdpt[p3] & 0x000FFFFFC0000000ULL) | (virt & 0x3FFFFFFF);
    uint64_t *pd   = phys_to_pt(pdpt[p3] & VMM_PHYS_MASK);
    if (!(pd[p2] & VMM_PRESENT)) return 0;
    if (pd[p2] & VMM_HUGE)
        return (pd[p2] & 0x000FFFFFFFE00000ULL) | (virt & 0x1FFFFF);
    uint64_t *pt   = phys_to_pt(pd[p2] & VMM_PHYS_MASK);
    if (!(pt[p1] & VMM_PRESENT)) return 0;
    return (pt[p1] & VMM_PHYS_MASK) | (virt & 0xFFF);
}

void vmm_switch(pml4_t pml4)
{
    uint64_t phys = (uint64_t)(uintptr_t)pml4;
    __asm__ __volatile__("mov %0, %%cr3" :: "r"(phys) : "memory");
}

pml4_t vmm_create_address_space(void)
{
    void *page = pmm_alloc();
    if (!page) return NULL;
    pml4_t pml4 = (pml4_t)(uintptr_t)page;
    /* Copy kernel upper-half entries (256-511) */
    for (int i = 256; i < 512; i++)
        pml4[i] = kernel_pml4[i];
    return pml4;
}

void vmm_init(void)
{
    void *pml4_page = pmm_alloc();
    if (!pml4_page) {
        serial_printf("[VMM] FATAL: cannot allocate PML4\n");
        for (;;) __asm__("cli;hlt");
    }
    kernel_pml4 = (pml4_t)(uintptr_t)pml4_page;

    /* Map first 128MB at higher half and keep identity map for low 2MB */
    for (uint64_t p = 0; p < 128ULL * 1024 * 1024; p += PAGE_SIZE)
        vmm_map(kernel_pml4, KERNEL_VIRT_BASE + p, p, VMM_KERNEL_RW);

    for (uint64_t p = 0; p < 2ULL * 1024 * 1024; p += PAGE_SIZE)
        vmm_map(kernel_pml4, p, p, VMM_KERNEL_RW);

    /* Enable NXE via cpu.h helpers (fixes broken "=A" rdmsr in 64-bit) */
    cpu_enable_nxe();

    vmm_switch(kernel_pml4);

    serial_printf("[VMM] PML4 @ 0x%p, higher-half active\n",
                  (void *)kernel_pml4);
}
