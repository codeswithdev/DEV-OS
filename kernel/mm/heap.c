/*
 * DevOS — Kernel Heap Allocator
 *
 * Two-tier design:
 *   Slab caches for <= 2048-byte allocations (power-of-2 sizes).
 *   Large allocator (page-granule linked list) for > 2048 bytes.
 *
 * The heap virtual region is grown on demand by calling pmm_alloc()
 * and mapping pages into the kernel address space via vmm_map().
 *
 * All allocations are prefixed with a 16-byte header aligned to
 * the allocation's natural power-of-2 boundary, ensuring that
 * the user pointer satisfies any x86_64 ABI requirement.
 *
 * Thread safety: single-CPU only in Phase 6. Phase 8 will add
 * a per-CPU slab cache and a global lock on the large allocator.
 */

#include "heap.h"
#include "pmm.h"
#include "vmm.h"
#include "vmm_ext.h"
#include "../arch/x86_64/serial.h"
#include "../lib/string.h"

/* ============================================================
 * Internal constants
 * ============================================================ */

#define LARGE_THRESHOLD     2048
#define HEAP_MAGIC_FREE     0xDEADC0DEDEADC0DEULL

/* Redefine as valid hex */
#undef HEAP_MAGIC_ALLOC
#define HEAP_MAGIC_ALLOC    0xA110CA11A110CA11ULL

/* ============================================================
 * Large allocator header (immediately before user pointer)
 * Must be 16-byte aligned so that user data after it is aligned.
 * ============================================================ */
typedef struct large_hdr {
    uint64_t         magic;     /* HEAP_MAGIC_ALLOC or HEAP_MAGIC_FREE */
    size_t           size;      /* total span size including this header */
    struct large_hdr *next;     /* free list link (valid when free) */
    struct large_hdr *prev;
} __attribute__((aligned(16))) large_hdr_t;

#define LARGE_HDR_SIZE  sizeof(large_hdr_t)   /* 32 bytes */

/* ============================================================
 * Slab cache
 *
 * Each slab page (4096 bytes) layout:
 *   [slab_page_t header] [bitmap: ceil(N/64) × 8 bytes] [objects...]
 *
 * Objects start at offset ALIGN_UP(sizeof(slab_page_t) + bitmap_bytes, obj_size).
 * This wastes a few bytes but keeps objects naturally aligned.
 * ============================================================ */
#define SLAB_PAGE_MAGIC     0x5AB000005AB00000ULL

typedef struct slab_page {
    uint64_t         magic;
    uint8_t          order;        /* size class: SLAB_MIN_ORDER .. SLAB_MAX_ORDER */
    uint16_t         obj_size;     /* 1 << order */
    uint16_t         capacity;     /* total objects in this slab page */
    uint16_t         free_count;
    uint16_t         first_free;   /* index hint */
    struct slab_page *next;        /* class free-list link */
    /* bitmap follows immediately, then objects */
} __attribute__((aligned(8))) slab_page_t;

#define SLAB_HDR_SIZE  ALIGN_UP(sizeof(slab_page_t), 8)

static inline uint64_t *slab_bitmap(slab_page_t *sp) {
    return (uint64_t *)((uint8_t *)sp + SLAB_HDR_SIZE);
}

static inline uint16_t slab_bitmap_words(slab_page_t *sp) {
    return (uint16_t)((sp->capacity + 63) / 64);
}

static inline void *slab_object(slab_page_t *sp, uint16_t idx) {
    uint16_t bmap_bytes = (uint16_t)(slab_bitmap_words(sp) * 8);
    uint32_t data_off   = (uint32_t)ALIGN_UP(SLAB_HDR_SIZE + bmap_bytes, sp->obj_size);
    return (uint8_t *)sp + data_off + (uint32_t)idx * sp->obj_size;
}

static slab_page_t *slab_classes[SLAB_CLASSES] = {0};

/* ============================================================
 * Large allocator state
 * ============================================================ */
static large_hdr_t *large_free_list = NULL;

/* ============================================================
 * Heap virtual address cursor
 * ============================================================ */
static uint64_t heap_cursor = HEAP_VIRT_BASE;
static uint64_t heap_limit  = HEAP_VIRT_BASE;

/* ============================================================
 * heap_map_pages — expand the heap by mapping n physical pages
 * Returns virtual pointer to the start of the new region, or NULL.
 * ============================================================ */
void *heap_map_pages(size_t count)
{
    uint64_t bytes = count * PAGE_SIZE;
    if (heap_cursor + bytes > HEAP_VIRT_BASE + HEAP_MAX_SIZE) {
        serial_printf("[HEAP] OOM: heap virtual space exhausted\n");
        return NULL;
    }

    for (size_t i = 0; i < count; i++) {
        void *phys = pmm_alloc();
        if (!phys) {
            serial_printf("[HEAP] OOM: pmm_alloc failed at page %zu\n", i);
            return NULL;
        }
        /* Map into kernel address space (current kernel PML4 = active CR3) */
        vmm_map_kernel(heap_cursor + i * PAGE_SIZE,
                       (uint64_t)(uintptr_t)phys,
                       VMM_KERNEL_RW | VMM_NO_EXEC);
    }

    void *result = (void *)heap_cursor;
    heap_cursor += bytes;
    heap_limit   = heap_cursor;
    return result;
}

/* ============================================================
 * slab_new_page — allocate and initialise a slab page for a class
 * ============================================================ */
static slab_page_t *slab_new_page(uint8_t order)
{
    slab_page_t *sp = (slab_page_t *)heap_map_pages(1);
    if (!sp) return NULL;

    uint16_t obj_size = (uint16_t)(1u << order);

    /* Calculate capacity */
    uint16_t cap_est  = (uint16_t)((PAGE_SIZE - SLAB_HDR_SIZE) / obj_size);
    /* Iteratively shrink for bitmap overhead */
    uint16_t cap = cap_est;
    for (;;) {
        uint16_t bmap_bytes = (uint16_t)(((cap + 63) / 64) * 8);
        uint32_t data_off   = (uint32_t)ALIGN_UP(SLAB_HDR_SIZE + bmap_bytes, obj_size);
        if (data_off + (uint32_t)cap * obj_size <= PAGE_SIZE) break;
        cap--;
    }

    sp->magic       = SLAB_PAGE_MAGIC;
    sp->order       = order;
    sp->obj_size    = obj_size;
    sp->capacity    = cap;
    sp->free_count  = cap;
    sp->first_free  = 0;
    sp->next        = NULL;

    /* Bitmap: 0 = free, 1 = used */
    memset(slab_bitmap(sp), 0, slab_bitmap_words(sp) * 8);

    return sp;
}

/* ============================================================
 * slab_alloc
 * ============================================================ */
static void *slab_alloc(uint8_t order)
{
    int cls = order - SLAB_MIN_ORDER;
    slab_page_t *sp = slab_classes[cls];

    /* Find a slab page with free slots */
    while (sp && sp->free_count == 0) sp = sp->next;

    if (!sp) {
        sp = slab_new_page(order);
        if (!sp) return NULL;
        sp->next = slab_classes[cls];
        slab_classes[cls] = sp;
    }

    /* Find free slot in bitmap */
    uint64_t *bmap  = slab_bitmap(sp);
    uint16_t  words = slab_bitmap_words(sp);
    uint16_t  idx   = sp->first_free;

    /* Start search from hint */
    uint16_t wi = idx / 64;
    for (uint16_t w = 0; w < words; w++) {
        uint16_t actual_w = (wi + w) % words;
        uint64_t free_mask = ~bmap[actual_w];
        if (!free_mask) continue;
        int bit = __builtin_ctzll(free_mask);
        idx = (uint16_t)(actual_w * 64 + bit);
        if (idx >= sp->capacity) continue;
        bmap[actual_w] |= (1ULL << bit);
        sp->free_count--;
        sp->first_free = idx;
        return slab_object(sp, idx);
    }

    /* Should not reach here */
    return NULL;
}

/* ============================================================
 * slab_free
 * Returns 1 if ptr was in a slab, 0 if not (it's a large alloc).
 * ============================================================ */
static int slab_free(void *ptr)
{
    /* Find which slab page owns this pointer.
     * The slab page is at the 4KB-aligned base of ptr. */
    slab_page_t *sp = (slab_page_t *)ALIGN_DOWN((uint64_t)(uintptr_t)ptr, PAGE_SIZE);

    /* Validate magic before treating as slab page */
    if (sp->magic != SLAB_PAGE_MAGIC) return 0;

    /* Confirm ptr is within this page's object array */
    uint16_t bmap_bytes = (uint16_t)(slab_bitmap_words(sp) * 8);
    uint32_t data_off   = (uint32_t)ALIGN_UP(SLAB_HDR_SIZE + bmap_bytes, sp->obj_size);
    uint8_t *objects    = (uint8_t *)sp + data_off;

    if ((uint8_t *)ptr < objects) return 0;
    size_t byte_off = (uint8_t *)ptr - objects;
    if (byte_off % sp->obj_size != 0) return 0;
    uint16_t idx = (uint16_t)(byte_off / sp->obj_size);
    if (idx >= sp->capacity) return 0;

    uint64_t *bmap = slab_bitmap(sp);
    if (!(bmap[idx / 64] & (1ULL << (idx % 64)))) {
        serial_printf("[HEAP] slab double-free @ %p idx=%u\n", ptr, idx);
        return 1;
    }
    bmap[idx / 64] &= ~(1ULL << (idx % 64));
    sp->free_count++;
    if (idx < sp->first_free) sp->first_free = idx;

    return 1;
}

/* ============================================================
 * Large allocator
 * ============================================================ */
static void large_insert_free(large_hdr_t *hdr)
{
    hdr->magic = HEAP_MAGIC_FREE;
    hdr->next  = large_free_list;
    hdr->prev  = NULL;
    if (large_free_list) large_free_list->prev = hdr;
    large_free_list = hdr;
}

static void large_remove_free(large_hdr_t *hdr)
{
    if (hdr->prev) hdr->prev->next = hdr->next;
    else           large_free_list  = hdr->next;
    if (hdr->next) hdr->next->prev = hdr->prev;
    hdr->next = hdr->prev = NULL;
}

static void *large_alloc(size_t size)
{
    size_t total = ALIGN_UP(size + LARGE_HDR_SIZE, PAGE_SIZE);

    /* Best-fit search */
    large_hdr_t *best = NULL;
    for (large_hdr_t *h = large_free_list; h; h = h->next) {
        if (h->size >= total) {
            if (!best || h->size < best->size) best = h;
        }
    }

    if (!best) {
        /* Expand heap */
        size_t pages = total / PAGE_SIZE;
        best = (large_hdr_t *)heap_map_pages(pages);
        if (!best) return NULL;
        best->size = pages * PAGE_SIZE;
        best->next = best->prev = NULL;
        best->magic = HEAP_MAGIC_FREE;
    } else {
        large_remove_free(best);
    }

    /* Split if remainder is large enough for a useful free block */
    if (best->size >= total + PAGE_SIZE + LARGE_HDR_SIZE) {
        large_hdr_t *rem = (large_hdr_t *)((uint8_t *)best + total);
        rem->size  = best->size - total;
        best->size = total;
        large_insert_free(rem);
    }

    best->magic = HEAP_MAGIC_ALLOC;
    best->next  = best->prev = NULL;
    return (uint8_t *)best + LARGE_HDR_SIZE;
}

static void large_free(void *ptr)
{
    large_hdr_t *hdr = (large_hdr_t *)((uint8_t *)ptr - LARGE_HDR_SIZE);
    if (hdr->magic == HEAP_MAGIC_FREE) {
        serial_printf("[HEAP] large double-free @ %p\n", ptr);
        return;
    }
    if (hdr->magic != HEAP_MAGIC_ALLOC) {
        serial_printf("[HEAP] large_free: corrupt header @ %p\n", ptr);
        return;
    }
    large_insert_free(hdr);
    /* TODO Phase 8: coalesce adjacent free blocks */
}

/* ============================================================
 * Public API
 * ============================================================ */

void heap_init(void)
{
    heap_cursor = HEAP_VIRT_BASE;
    heap_limit  = HEAP_VIRT_BASE;
    large_free_list = NULL;
    for (int i = 0; i < SLAB_CLASSES; i++) slab_classes[i] = NULL;

    /* Pre-map 4 pages to warm up the allocator */
    heap_map_pages(4);

    /* Give the pre-mapped pages to the large allocator as one free block */
    large_hdr_t *init = (large_hdr_t *)HEAP_VIRT_BASE;
    init->size  = 4 * PAGE_SIZE;
    init->next  = init->prev = NULL;
    init->magic = HEAP_MAGIC_FREE;
    large_free_list = init;

    serial_printf("[HEAP] init @ 0x%016llx, %u slab classes\n",
                  HEAP_VIRT_BASE, SLAB_CLASSES);
}

void *kmalloc(size_t size)
{
    if (size == 0) return NULL;

    if (size <= LARGE_THRESHOLD) {
        /* Find smallest slab class >= size */
        uint8_t order = SLAB_MIN_ORDER;
        while ((1u << order) < size && order <= SLAB_MAX_ORDER) order++;
        if (order <= SLAB_MAX_ORDER) return slab_alloc(order);
    }

    return large_alloc(size);
}

void *kzalloc(size_t size)
{
    void *p = kmalloc(size);
    if (p) memset(p, 0, size);
    return p;
}

void kfree(void *ptr)
{
    if (!ptr) return;

    /* Try slab first (O(1) check via page magic) */
    if (slab_free(ptr)) return;

    /* Fall through to large allocator */
    large_free(ptr);
}

void *krealloc(void *ptr, size_t new_size)
{
    if (!ptr)     return kmalloc(new_size);
    if (!new_size) { kfree(ptr); return NULL; }

    /* Check if it's a large block and get current size */
    slab_page_t *sp = (slab_page_t *)ALIGN_DOWN((uint64_t)(uintptr_t)ptr, PAGE_SIZE);
    size_t old_size;

    if (sp->magic == SLAB_PAGE_MAGIC) {
        old_size = sp->obj_size;
    } else {
        large_hdr_t *hdr = (large_hdr_t *)((uint8_t *)ptr - LARGE_HDR_SIZE);
        old_size = hdr->size - LARGE_HDR_SIZE;
    }

    if (new_size <= old_size) return ptr;

    void *new_ptr = kmalloc(new_size);
    if (!new_ptr) return NULL;
    memcpy(new_ptr, ptr, old_size);
    kfree(ptr);
    return new_ptr;
}

void heap_dump_stats(void)
{
    size_t large_free_bytes = 0;
    size_t large_free_blocks = 0;
    for (large_hdr_t *h = large_free_list; h; h = h->next) {
        large_free_bytes += h->size;
        large_free_blocks++;
    }
    serial_printf("[HEAP] cursor=0x%llx large_free=%zu blocks=%zu\n",
                  heap_cursor, large_free_bytes, large_free_blocks);
    for (int i = 0; i < SLAB_CLASSES; i++) {
        uint16_t obj_size = (uint16_t)(1u << (SLAB_MIN_ORDER + i));
        int pages = 0;
        for (slab_page_t *sp = slab_classes[i]; sp; sp = sp->next) pages++;
        if (pages) serial_printf("  slab[%4u]: %d pages\n", obj_size, pages);
    }
}
