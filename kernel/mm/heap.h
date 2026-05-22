#pragma once
#include "../include/types.h"

/*
 * DevOS kernel heap — two-tier allocator.
 *
 * Tier 1 (slab): fixed-size power-of-2 caches 16..2048 bytes.
 *   Each slab page holds a bitmap + N same-size objects.
 *   O(1) alloc/free for the common case.
 *
 * Tier 2 (page-granule): allocations > 2048 bytes or > one page.
 *   Simple header-linked list of free spans.
 *   Suitable for task stacks, ELF segments, page table pages.
 *
 * Virtual range: HEAP_VIRT_BASE .. HEAP_VIRT_BASE + HEAP_MAX_SIZE
 *   0xFFFFFF8000000000 – +256MB (comfortably below kernel image at -2GB)
 */

#define HEAP_VIRT_BASE      0xFFFFFF8000000000ULL
#define HEAP_MAX_SIZE       (256ULL * 1024 * 1024)   /* 256 MB */

/* Slab size classes: 16, 32, 64, 128, 256, 512, 1024, 2048 */
#define SLAB_MIN_ORDER      4   /* 2^4  = 16  */
#define SLAB_MAX_ORDER      11  /* 2^11 = 2048 */
#define SLAB_CLASSES        (SLAB_MAX_ORDER - SLAB_MIN_ORDER + 1)

void  heap_init(void);
void *kmalloc(size_t size);
void *kzalloc(size_t size);
void  kfree(void *ptr);
void *krealloc(void *ptr, size_t new_size);

/* Extend heap by mapping additional physical pages */
void *heap_map_pages(size_t count);

/* Debug */
void heap_dump_stats(void);
