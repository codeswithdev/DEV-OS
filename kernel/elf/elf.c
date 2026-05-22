/*
 * DevOS — ELF64 Binary Loader
 *
 * Parses ELF64 headers, validates, maps PT_LOAD segments into a new
 * process address space, creates a user stack, and spawns a user task.
 *
 * Supports: statically-linked ELF64 executables (ET_EXEC).
 * Does not support: dynamic linking, relocations, ELF64 shared objects.
 *
 * Memory model:
 *   A new PML4 is created per process (vmm_create_address_space).
 *   PT_LOAD segments are mapped with per-segment permissions derived
 *   from the ELF p_flags field.
 *   BSS region (p_memsz > p_filesz) is zero-filled via pmm_alloc_zeroed.
 *   User stack is mapped at USER_STACK_TOP - USER_STACK_SIZE.
 *
 * After loading, sched_create_user_task() is called with:
 *   entry_rip = elf_header.e_entry
 *   user_rsp  = USER_STACK_TOP - 8 (aligned, with null sentinel)
 *   pml4_phys = physical address of new PML4
 */

#include "elf.h"
#include "../mm/vmm.h"
#include "../mm/vmm_ext.h"
#include "../mm/pmm.h"
#include "../mm/heap.h"
#include "../sched/sched.h"
#include "../arch/x86_64/serial.h"
#include "../lib/string.h"

/* ============================================================
 * ELF64 structure definitions (no external headers)
 * ============================================================ */

#define ELF_MAGIC       0x464C457F   /* "\x7fELF" as little-endian u32 */

#define ET_EXEC         2
#define EM_X86_64       62
#define EV_CURRENT      1
#define ELFCLASS64      2
#define ELFDATA2LSB     1   /* little-endian */

/* ELF64 file header */
typedef struct {
    uint8_t  e_ident[16];
    uint16_t e_type;
    uint16_t e_machine;
    uint32_t e_version;
    uint64_t e_entry;
    uint64_t e_phoff;       /* program header table offset */
    uint64_t e_shoff;
    uint32_t e_flags;
    uint16_t e_ehsize;
    uint16_t e_phentsize;
    uint16_t e_phnum;
    uint16_t e_shentsize;
    uint16_t e_shnum;
    uint16_t e_shstrndx;
} __attribute__((packed)) elf64_hdr_t;

/* ELF64 program header */
typedef struct {
    uint32_t p_type;
    uint32_t p_flags;
    uint64_t p_offset;  /* offset in file */
    uint64_t p_vaddr;   /* virtual address in process */
    uint64_t p_paddr;   /* physical address (ignored) */
    uint64_t p_filesz;  /* size in file */
    uint64_t p_memsz;   /* size in memory (>= p_filesz; BSS padding) */
    uint64_t p_align;
} __attribute__((packed)) elf64_phdr_t;

/* Program header types */
#define PT_NULL     0
#define PT_LOAD     1
#define PT_DYNAMIC  2
#define PT_INTERP   3

/* Program header flags */
#define PF_X        (1u << 0)   /* execute */
#define PF_W        (1u << 1)   /* write */
#define PF_R        (1u << 2)   /* read */

/* ============================================================
 * elf_flags_to_vmm — convert ELF p_flags to VMM page flags
 * ============================================================ */
static uint64_t elf_flags_to_vmm(uint32_t pf)
{
    uint64_t flags = VMM_PRESENT | VMM_USER;
    if (pf & PF_W)        flags |= VMM_WRITABLE;
    if (!(pf & PF_X))     flags |= VMM_NO_EXEC;
    return flags;
}

/* ============================================================
 * elf_validate — sanity-check the ELF header
 * ============================================================ */
static int elf_validate(const elf64_hdr_t *hdr, size_t size)
{
    if (size < sizeof(elf64_hdr_t)) {
        serial_printf("[ELF] too small: %zu bytes\n", size);
        return -1;
    }

    /* Magic: e_ident[0..3] = 0x7f 'E' 'L' 'F' */
    uint32_t magic = *(const uint32_t *)hdr->e_ident;
    if (magic != ELF_MAGIC) {
        serial_printf("[ELF] bad magic: 0x%08x\n", magic);
        return -1;
    }

    if (hdr->e_ident[4] != ELFCLASS64) {
        serial_printf("[ELF] not 64-bit (class=%u)\n", hdr->e_ident[4]);
        return -1;
    }
    if (hdr->e_ident[5] != ELFDATA2LSB) {
        serial_printf("[ELF] not little-endian\n");
        return -1;
    }
    if (hdr->e_type != ET_EXEC) {
        serial_printf("[ELF] not ET_EXEC (type=%u)\n", hdr->e_type);
        return -1;
    }
    if (hdr->e_machine != EM_X86_64) {
        serial_printf("[ELF] not x86-64 (machine=%u)\n", hdr->e_machine);
        return -1;
    }
    if (hdr->e_version != EV_CURRENT) {
        serial_printf("[ELF] bad version\n");
        return -1;
    }
    if (hdr->e_phentsize < sizeof(elf64_phdr_t)) {
        serial_printf("[ELF] phentsize too small\n");
        return -1;
    }
    if (hdr->e_phnum == 0) {
        serial_printf("[ELF] no program headers\n");
        return -1;
    }
    if (hdr->e_entry == 0) {
        serial_printf("[ELF] zero entry point\n");
        return -1;
    }

    return 0;
}

/* ============================================================
 * elf_map_segment — map one PT_LOAD segment into the address space
 *
 * Each PT_LOAD segment occupies [p_vaddr, p_vaddr + p_memsz) in the
 * process address space. The range must be page-aligned for mapping.
 *
 * File data (p_filesz bytes) is copied page-by-page from elf_data.
 * BSS (p_memsz - p_filesz bytes) is already zero (pmm_alloc zeroes).
 * ============================================================ */
static int elf_map_segment(pml4_t pml4,
                            const uint8_t *elf_data, size_t elf_size,
                            const elf64_phdr_t *ph)
{
    if (ph->p_memsz == 0) return 0;

    /* Validate bounds */
    if (ph->p_filesz > 0) {
        if (ph->p_offset + ph->p_filesz > elf_size) {
            serial_printf("[ELF] segment extends beyond file\n");
            return -1;
        }
    }

    uint64_t va_start = ALIGN_DOWN(ph->p_vaddr, PAGE_SIZE);
    uint64_t va_end   = ALIGN_UP(ph->p_vaddr + ph->p_memsz, PAGE_SIZE);
    uint64_t num_pages = (va_end - va_start) / PAGE_SIZE;
    uint64_t vmm_flags = elf_flags_to_vmm(ph->p_flags);

    serial_printf("[ELF] PT_LOAD vaddr=0x%llx filesz=0x%llx memsz=0x%llx flags=%c%c%c\n",
                  ph->p_vaddr, ph->p_filesz, ph->p_memsz,
                  (ph->p_flags & PF_R) ? 'R' : '-',
                  (ph->p_flags & PF_W) ? 'W' : '-',
                  (ph->p_flags & PF_X) ? 'X' : '-');

    /*
     * Allocate and map physical pages for this segment.
     * We map page-by-page, copying file data into each physical page
     * via the identity map (phys addr == virt addr for low memory).
     * PMM guarantees zeroed pages, so BSS is handled automatically.
     */
    const uint8_t *src = elf_data + ph->p_offset;

    for (uint64_t i = 0; i < num_pages; i++) {
        void *phys_page = pmm_alloc();  /* zeroed by PMM */
        if (!phys_page) {
            serial_printf("[ELF] OOM mapping segment page %llu\n", i);
            return -1;
        }

        uint64_t page_va   = va_start + i * PAGE_SIZE;
        uint64_t page_phys = (uint64_t)(uintptr_t)phys_page;

        /* Map this page into the process address space */
        vmm_map(pml4, page_va, page_phys, vmm_flags);

        /*
         * Copy file data into this physical page.
         * The physical page is accessible via identity map.
         * Compute overlap of [p_vaddr, p_vaddr+p_filesz) with this page.
         */
        uint64_t page_start = page_va;
        uint64_t page_end   = page_va + PAGE_SIZE;
        uint64_t seg_start  = ph->p_vaddr;
        uint64_t seg_fend   = ph->p_vaddr + ph->p_filesz;

        /* File-data range for this page */
        uint64_t copy_va_start = (seg_start > page_start) ? seg_start : page_start;
        uint64_t copy_va_end   = (seg_fend  < page_end)   ? seg_fend  : page_end;

        if (copy_va_start < copy_va_end && copy_va_start < seg_fend) {
            uint64_t dst_off  = copy_va_start - page_start;  /* offset in phys page */
            uint64_t src_off  = copy_va_start - seg_start;   /* offset in file data */
            uint64_t copy_len = copy_va_end - copy_va_start;

            uint8_t *dst = (uint8_t *)(uintptr_t)page_phys + dst_off;
            memcpy(dst, src + src_off, copy_len);
        }
        /* Remaining bytes in page are zero (PMM zeroed on alloc) */
    }

    return 0;
}

/* ============================================================
 * elf_load — main entry point
 * ============================================================ */
task_t *elf_load(const char *name, const void *elf_data, size_t elf_size,
                 uint32_t priority)
{
    const elf64_hdr_t *hdr = (const elf64_hdr_t *)elf_data;

    if (elf_validate(hdr, elf_size) != 0) return NULL;

    serial_printf("[ELF] loading '%s' entry=0x%llx phnum=%u\n",
                  name, hdr->e_entry, hdr->e_phnum);

    /* Create new process address space */
    pml4_t pml4 = vmm_create_address_space();
    if (!pml4) {
        serial_printf("[ELF] failed to create address space\n");
        return NULL;
    }

    /*
     * vmm_create_address_space returns a pointer via phys_to_virt(phys).
     * Since phys_to_virt is the identity map (phys == virt for low memory),
     * the physical address is the same as the pointer value.
     */
    uint64_t pml4_phys = (uint64_t)(uintptr_t)pml4;

    /* Map all PT_LOAD segments */
    const uint8_t *data = (const uint8_t *)elf_data;
    for (uint16_t i = 0; i < hdr->e_phnum; i++) {
        const elf64_phdr_t *ph = (const elf64_phdr_t *)
            (data + hdr->e_phoff + (uint64_t)i * hdr->e_phentsize);

        if (ph->p_type != PT_LOAD) continue;

        if (elf_map_segment(pml4, data, elf_size, ph) != 0) {
            serial_printf("[ELF] failed to map segment %u\n", i);
            /* TODO: free already-mapped pages */
            return NULL;
        }
    }

    /* Map user stack */
    uint64_t stack_bottom = USER_STACK_TOP - USER_STACK_SIZE;
    serial_printf("[ELF] mapping user stack [0x%llx, 0x%llx)\n",
                  stack_bottom, (uint64_t)USER_STACK_TOP);

    if (vmm_alloc_range(pml4, stack_bottom, USER_STACK_SIZE,
                         VMM_USER_RW | VMM_NO_EXEC) != 0) {
        serial_printf("[ELF] failed to map user stack\n");
        return NULL;
    }

    /*
     * User RSP: top of stack, minus 8 for the null return address
     * that x86-64 ABI expects at the bottom of the main() frame.
     * Must be 16-byte aligned before the call instruction that
     * invokes main, so RSP at _start must be 16-byte aligned.
     * Subtract 8 so that after the implicit CALL in crt pushes
     * the return address, RSP is 16-byte aligned inside main().
     */
    uint64_t user_rsp = USER_STACK_TOP - 8;

    /* Write null return address at user_rsp.
     * user_rsp is in the user address space; we write to it via
     * the physical page obtained by walking the new PML4. */
    uint64_t rsp_phys = vmm_translate(pml4, user_rsp & ~7ULL);
    if (rsp_phys) {
        uint64_t *null_ret = (uint64_t *)(uintptr_t)
            (rsp_phys + (user_rsp & 0xFFF));
        *null_ret = 0;
    }

    /* Create and enqueue user task */
    task_t *t = sched_create_user_task(name, hdr->e_entry, user_rsp,
                                        pml4_phys, priority);
    if (!t) {
        serial_printf("[ELF] failed to create user task\n");
        return NULL;
    }

    sched_enqueue(t);
    serial_printf("[ELF] '%s' loaded: entry=0x%llx rsp=0x%llx pid=%u\n",
                  name, hdr->e_entry, user_rsp, t->pid);
    return t;
}
