/*
 * PMM — Bitmap physical frame allocator.
 * Bitmap lives in BSS (1 bit per 4KB frame, up to 4GB = 128KB bitmap).
 * Zeroes each allocated page before returning.
 */

#include "pmm.h"
#include "../arch/x86_64/serial.h"
#include "../lib/string.h"

#define PMM_MAX_FRAMES      (1ULL * 1024 * 1024)   /* 4GB / 4KB */
#define BITMAP_WORDS        (PMM_MAX_FRAMES / 64)

/* 0 = free, 1 = used */
static uint64_t pmm_bitmap[BITMAP_WORDS];
static uint64_t total_frames = 0;
static uint64_t free_frames  = 0;
static uint64_t search_hint  = 0;   /* next word to check */

static inline void frame_set(uint64_t frame) {
    pmm_bitmap[frame / 64] |= (1ULL << (frame % 64));
}
static inline void frame_clear(uint64_t frame) {
    pmm_bitmap[frame / 64] &= ~(1ULL << (frame % 64));
}
static inline int frame_used(uint64_t frame) {
    return !!(pmm_bitmap[frame / 64] & (1ULL << (frame % 64)));
}

void pmm_init(boot_info_t *bi, uint64_t kernel_end_phys)
{
    /* Mark everything used initially */
    memset(pmm_bitmap, 0xFF, sizeof(pmm_bitmap));

    /* Free E820 USABLE regions */
    for (uint32_t i = 0; i < bi->e820_count; i++) {
        e820_entry_t *e = &bi->e820_map[i];
        if (e->type != 1) continue;

        uint64_t start = ALIGN_UP(e->base, PAGE_SIZE);
        uint64_t end   = ALIGN_DOWN(e->base + e->length, PAGE_SIZE);
        if (end <= start) continue;

        for (uint64_t addr = start; addr < end; addr += PAGE_SIZE) {
            uint64_t frame = addr / PAGE_SIZE;
            if (frame >= PMM_MAX_FRAMES) break;
            frame_clear(frame);
            total_frames++;
            free_frames++;
        }
    }

    /* Reserve frame 0 (null/BIOS) */
    if (!frame_used(0)) { frame_set(0); free_frames--; }

    /* Reserve kernel region */
    uint64_t kstart = ALIGN_DOWN(0x100000, PAGE_SIZE);
    uint64_t kend   = ALIGN_UP(kernel_end_phys, PAGE_SIZE);
    for (uint64_t a = kstart; a < kend; a += PAGE_SIZE) {
        uint64_t f = a / PAGE_SIZE;
        if (!frame_used(f)) { frame_set(f); free_frames--; }
    }

    serial_printf("[PMM] %llu MB usable, %llu MB free after kernel\n",
                  (total_frames * PAGE_SIZE) >> 20,
                  (free_frames  * PAGE_SIZE) >> 20);
}

void *pmm_alloc(void)
{
    uint64_t words = PMM_MAX_FRAMES / 64;

    for (uint64_t wi = 0; wi < words; wi++) {
        uint64_t actual = (search_hint + wi) % words;
        if (pmm_bitmap[actual] == ~0ULL) continue;

        int bit = __builtin_ctzll(~pmm_bitmap[actual]);
        uint64_t frame = actual * 64 + (uint64_t)bit;
        if (frame >= PMM_MAX_FRAMES) continue;

        frame_set(frame);
        free_frames--;
        search_hint = actual;

        uint64_t phys = frame * PAGE_SIZE;
        /* Zero the page — accessible via identity map (low phys = low virt) */
        memset((void *)(uintptr_t)phys, 0, PAGE_SIZE);
        return (void *)(uintptr_t)phys;
    }

    serial_printf("[PMM] OOM — no free frames!\n");
    return NULL;
}

void pmm_free_page(void *phys)
{
    uint64_t frame = (uint64_t)(uintptr_t)phys / PAGE_SIZE;
    if (frame == 0 || frame >= PMM_MAX_FRAMES) return;
    if (!frame_used(frame)) {
        serial_printf("[PMM] double-free frame %llu\n", frame);
        return;
    }
    frame_clear(frame);
    free_frames++;
}

uint64_t pmm_free_pages(void)  { return free_frames;  }
uint64_t pmm_total_pages(void) { return total_frames; }
