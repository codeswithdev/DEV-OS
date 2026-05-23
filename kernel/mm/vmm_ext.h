#pragma once
#include "../include/types.h"
#include "vmm.h"

void     vmm_map_kernel(uint64_t virt, uint64_t phys, uint64_t flags);
uint64_t vmm_alloc_map(pml4_t pml4, uint64_t virt, uint64_t flags);
int      vmm_alloc_range(pml4_t pml4, uint64_t virt, size_t size, uint64_t flags);
void     vmm_free_range(pml4_t pml4, uint64_t virt, size_t size);
