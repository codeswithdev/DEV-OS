#pragma once
#include "../include/types.h"
#include "../include/boot_info.h"

void  pmm_init(boot_info_t *bi, uint64_t kernel_end_phys);
void *pmm_alloc(void);   /* returns zeroed 4KB page, or NULL */
void  pmm_free_page(void *phys);
uint64_t pmm_free_pages(void);
uint64_t pmm_total_pages(void);
