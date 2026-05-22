#pragma once
#include "types.h"

#define BOOT_INFO_MAGIC     0xB007B007U
#define E820_MAX_ENTRIES    32

typedef struct {
    uint64_t base;
    uint64_t length;
    uint32_t type;
    uint32_t acpi_attrs;
} PACKED e820_entry_t;

typedef struct {
    uint32_t    magic;
    uint32_t    e820_count;
    e820_entry_t e820_map[E820_MAX_ENTRIES];
    uint64_t    kernel_phys_start;
    uint64_t    kernel_phys_end;
    uint64_t    fb_addr;
    uint32_t    fb_width;
    uint32_t    fb_height;
    uint32_t    fb_pitch;
    uint32_t    fb_bpp;
} PACKED boot_info_t;
