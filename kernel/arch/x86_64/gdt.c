#include "gdt.h"
#include "../../lib/string.h"
#include "serial.h"

typedef struct {
    uint32_t reserved0;
    uint64_t rsp0;
    uint64_t rsp1;
    uint64_t rsp2;
    uint64_t reserved1;
    uint64_t ist[7];
    uint64_t reserved2;
    uint16_t reserved3;
    uint16_t iopb_offset;
} PACKED tss64_t;

typedef struct {
    uint16_t limit_low;
    uint16_t base_low;
    uint8_t  base_mid;
    uint8_t  access;
    uint8_t  gran;
    uint8_t  base_high;
} PACKED gdt_entry_t;

typedef struct {
    uint16_t limit_low;
    uint16_t base_0_15;
    uint8_t  base_16_23;
    uint8_t  type;
    uint8_t  limit_flags;
    uint8_t  base_24_31;
    uint32_t base_32_63;
    uint32_t reserved;
} PACKED tss_descriptor_t;

static union {
    gdt_entry_t e[GDT_ENTRIES];
    uint8_t     raw[GDT_ENTRIES * 8];
} gdt __attribute__((aligned(16)));

static struct {
    uint16_t limit;
    uint64_t base;
} PACKED gdtr;

static tss64_t tss __attribute__((aligned(16)));

static void gdt_set_entry(int idx, uint32_t base, uint32_t limit,
                           uint8_t access, uint8_t gran)
{
    gdt_entry_t *e = &gdt.e[idx];
    e->limit_low = (uint16_t)(limit & 0xFFFF);
    e->base_low  = (uint16_t)(base  & 0xFFFF);
    e->base_mid  = (uint8_t)((base  >> 16) & 0xFF);
    e->access    = access;
    e->gran      = (uint8_t)(((limit >> 16) & 0x0F) | (gran & 0xF0));
    e->base_high = (uint8_t)((base  >> 24) & 0xFF);
}

void gdt_set_ist(uint8_t slot, uint64_t stack_top)
{
    if (slot < 1 || slot > 7) return;
    tss.ist[slot - 1] = stack_top;
}

void gdt_set_kernel_stack(uint64_t stack_top)
{
    tss.rsp0 = stack_top;
}

void gdt_init(void)
{
    memset(&gdt, 0, sizeof(gdt));
    memset(&tss, 0, sizeof(tss));
    tss.iopb_offset = (uint16_t)sizeof(tss);

    gdt_set_entry(GDT_NULL,        0, 0,       0,    0);
    gdt_set_entry(GDT_KERNEL_CODE, 0, 0xFFFFF, 0x9A, 0x20);
    gdt_set_entry(GDT_KERNEL_DATA, 0, 0xFFFFF, 0x92, 0x00);
    gdt_set_entry(GDT_USER_DATA,   0, 0xFFFFF, 0xF2, 0x00);
    gdt_set_entry(GDT_USER_CODE,   0, 0xFFFFF, 0xFA, 0x20);

    /* TSS system descriptor (16-byte) */
    {
        uintptr_t   base  = (uintptr_t)&tss;
        uint32_t    limit = (uint32_t)(sizeof(tss) - 1);
        tss_descriptor_t *td = (tss_descriptor_t *)&gdt.e[GDT_TSS_LOW];
        td->limit_low   = (uint16_t)(limit & 0xFFFF);
        td->base_0_15   = (uint16_t)(base  & 0xFFFF);
        td->base_16_23  = (uint8_t)((base  >> 16) & 0xFF);
        td->type        = 0x89;
        td->limit_flags = (uint8_t)((limit >> 16) & 0x0F);
        td->base_24_31  = (uint8_t)((base  >> 24) & 0xFF);
        td->base_32_63  = (uint32_t)(base  >> 32);
        td->reserved    = 0;
    }

    gdtr.limit = (uint16_t)(sizeof(gdt) - 1);
    gdtr.base  = (uint64_t)(uintptr_t)gdt.raw;

    __asm__ __volatile__(
        "lgdt %0\n\t"
        /* Reload data segments with kernel data selector */
        "mov %w1, %%ax\n\t"
        "mov %%ax, %%ds\n\t"
        "mov %%ax, %%es\n\t"
        "mov %%ax, %%ss\n\t"
        "xor %%eax, %%eax\n\t"
        "mov %%ax, %%fs\n\t"
        "mov %%ax, %%gs\n\t"
        /* Far return to reload CS with kernel code selector */
        "subq $16, %%rsp\n\t"
        "movq %2, 8(%%rsp)\n\t"   /* CS */
        "leaq 1f(%%rip), %%rax\n\t"
        "movq %%rax, 0(%%rsp)\n\t" /* RIP */
        "lretq\n\t"
        "1:\n\t"
        :
        : "m"(gdtr),
          "r"((uint64_t)SEL_KERNEL_DATA),
          "r"((uint64_t)SEL_KERNEL_CODE)
        : "rax", "memory"
    );

    __asm__ __volatile__(
        "mov %w0, %%ax\n\t"
        "ltr %%ax\n\t"
        :: "r"((uint32_t)SEL_TSS) : "ax"
    );

    serial_printf("[GDT] base=0x%p limit=0x%04x\n",
                  (void*)(uintptr_t)gdtr.base, gdtr.limit);
}
