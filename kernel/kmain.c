#include "include/types.h"
#include "include/boot_info.h"
#include "include/panic.h"
#include "arch/x86_64/serial.h"
#include "arch/x86_64/gdt.h"
#include "arch/x86_64/idt.h"
#include "arch/x86_64/irq.h"
#include "arch/x86_64/timer.h"
#include "arch/x86_64/cpu.h"
#include "drivers/vga.h"
#include "drivers/keyboard.h"
#include "mm/pmm.h"
#include "mm/vmm.h"
#include "mm/heap.h"
#include "sched/sched.h"
#include "syscall/syscall.h"
#include "elf/elf.h"
#include "fs/vfs.h"
#include "proc/proc.h"
#include "shell/shell.h"

extern uint64_t _kernel_phys_end;

extern uint8_t _binary_build_userland_test_elf_start[];
extern uint8_t _binary_build_userland_test_elf_end[];

static const char *e820_type_str(uint32_t t)
{
    switch (t) {
        case 1: return "USABLE";
        case 2: return "RESERVED";
        case 3: return "ACPI_RECLAIM";
        case 4: return "ACPI_NVS";
        default: return "UNKNOWN";
    }
}

static void parse_boot_info(boot_info_t *bi)
{
    serial_printf("[E820] %u entries:\n", bi->e820_count);
    for (uint32_t i = 0; i < bi->e820_count; i++) {
        e820_entry_t *e = &bi->e820_map[i];
        serial_printf("  [%2u] 0x%016llx + 0x%016llx  %s\n",
                      i, e->base, e->length, e820_type_str(e->type));
    }
}

void kmain(boot_info_t *bi)
{
    serial_init();
    serial_puts("\n[DevOS] Boot OK — serial online\n");

    if (!bi || bi->magic != BOOT_INFO_MAGIC)
        PANIC("Invalid boot_info (magic=0x%x)", bi ? bi->magic : 0);

    vga_init();
    vga_set_color(VGA_COLOR_LIGHT_CYAN, VGA_COLOR_BLACK);
    vga_puts("DevOS");
    vga_set_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK);
    vga_puts(" v0.4 — Booting...\n");

    gdt_init();    serial_puts("[GDT]    OK\n");
    idt_init();    serial_puts("[IDT]    OK\n");
    pic_init();    serial_puts("[PIC]    OK\n");

    parse_boot_info(bi);

    uint64_t kend = (uint64_t)(uintptr_t)&_kernel_phys_end;
    pmm_init(bi, kend);  serial_puts("[PMM]    OK\n");
    vmm_init();          serial_puts("[VMM]    OK\n");
    heap_init();         serial_puts("[HEAP]   OK\n");
    sched_init();        serial_puts("[SCHED]  OK\n");
    syscall_init();      serial_puts("[SYSCALL]OK\n");
    timer_init();        serial_puts("[TIMER]  OK\n");
    proc_init();         serial_puts("[PROC]   OK\n");
    vfs_init();          serial_puts("[VFS]    OK\n");
    keyboard_init();     serial_puts("[KB]     OK\n");

    cpu_enable_interrupts();
    serial_puts("[INIT] Interrupts enabled\n");

    vga_set_color(VGA_COLOR_LIGHT_GREEN, VGA_COLOR_BLACK);
    vga_puts("[OK] Kernel online — DevOS v0.4\n");
    vga_set_color(VGA_COLOR_WHITE, VGA_COLOR_BLACK);

    /* Load embedded userspace ELF if present */
    uint8_t *elf_start = _binary_build_userland_test_elf_start;
    uint8_t *elf_end   = _binary_build_userland_test_elf_end;
    size_t   elf_sz    = (size_t)(elf_end - elf_start);

    if (elf_end > elf_start && elf_sz >= 64) {
        serial_printf("[KMAIN] Loading userspace ELF (%llu bytes)\n",
                      (uint64_t)elf_sz);
        task_t *ut = elf_load("user_test", elf_start, elf_sz, 5);
        if (ut) {
            proc_create("user_test", ut);
            serial_printf("[KMAIN] user_test pid=%u\n", ut->pid);
        } else {
            serial_puts("[KMAIN] ELF load failed\n");
        }
    } else {
        serial_puts("[KMAIN] No embedded ELF\n");
    }

    /* Launch interactive shell as a kernel task */
    task_t *sh = sched_create_task("shell", shell_run, 10);
    if (sh) sched_enqueue(sh);

    serial_puts("[KMAIN] Entering scheduler\n");
    sched_yield();

    /* Should never reach here — idle loop */
    for (;;) cpu_halt();
}
