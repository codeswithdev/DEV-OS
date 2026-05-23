/*
 * vmm_ext.c — VMM extensions added in Phase 2
 *
 * Provides vmm_map_kernel() for heap and other kernel subsystems
 * that need to map pages into the kernel address space without
 * holding a pml4_t reference (they use the active CR3).
 *
 * Also provides vmm_alloc_user_pages() for ELF loader.
 */

#include "vmm.h"
#include "pmm.h"
#include "../arch/x86_64/serial.h"

/* Defined in vmm.c — we link against it directly */
extern uint64_t *kernel_pml4;

/*
 * vmm_map_kernel — map virt→phys in the global kernel PML4.
 * Used by heap_map_pages(). The kernel PML4 is the active one
 * for all kernel threads, so this is immediately visible.
 */
void vmm_map_kernel(uint64_t virt, uint64_t phys, uint64_t flags)
{
    vmm_map((pml4_t)kernel_pml4, virt, phys, flags);
}

/*
 * vmm_alloc_map — allocate a physical page and map it at virt
 * in the given address space. Returns physical address or 0.
 */
uint64_t vmm_alloc_map(pml4_t pml4, uint64_t virt, uint64_t flags)
{
    void *phys = pmm_alloc();
    if (!phys) return 0;
    vmm_map(pml4, virt, (uint64_t)(uintptr_t)phys, flags);
    return (uint64_t)(uintptr_t)phys;
}

/*
 * vmm_alloc_range — allocate and map a contiguous virtual range
 * with individual (possibly non-contiguous) physical pages.
 * Returns 0 on success, -1 on OOM.
 */
int vmm_alloc_range(pml4_t pml4, uint64_t virt, size_t size, uint64_t flags)
{
    uint64_t pages = ALIGN_UP(size, PAGE_SIZE) / PAGE_SIZE;
    for (uint64_t i = 0; i < pages; i++) {
        if (!vmm_alloc_map(pml4, virt + i * PAGE_SIZE, flags)) {
            /* TODO: unmap already-mapped pages on failure */
            return -1;
        }
    }
    return 0;
}

/*
 * vmm_free_range — unmap and free a range of pages.
 * Walks page tables, frees physical frames, unmaps entries.
 */
void vmm_free_range(pml4_t pml4, uint64_t virt, size_t size)
{
    uint64_t pages = ALIGN_UP(size, PAGE_SIZE) / PAGE_SIZE;
    for (uint64_t i = 0; i < pages; i++) {
        uint64_t va   = virt + i * PAGE_SIZE;
        uint64_t phys = vmm_translate(pml4, va);
        if (phys) {
            vmm_unmap(pml4, va);
            pmm_free_page((void *)phys);
        }
    }
}
