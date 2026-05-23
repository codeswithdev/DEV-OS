/* DevOS Kernel Linker Script
 * Kernel loaded at physical 0x100000, linked at 0xFFFFFFFF80100000
 */

KERNEL_PHYS_BASE = 0x100000;
KERNEL_VIRT_BASE = 0xFFFFFFFF80000000;

ENTRY(_start)

SECTIONS {
    /* Virtual base = phys + offset */
    . = KERNEL_VIRT_BASE + KERNEL_PHYS_BASE;

    _kernel_virt_start = .;
    _kernel_phys_start = . - KERNEL_VIRT_BASE;

    .boot_entry : AT(ADDR(.boot_entry) - KERNEL_VIRT_BASE) {
        /* Kernel entry point must be first */
        KEEP(*(.boot_entry))
    }

    .text : AT(ADDR(.text) - KERNEL_VIRT_BASE) {
        *(.text .text.*)
    }

    . = ALIGN(0x1000);
    .rodata : AT(ADDR(.rodata) - KERNEL_VIRT_BASE) {
        *(.rodata .rodata.*)
    }

    . = ALIGN(0x1000);
    .data : AT(ADDR(.data) - KERNEL_VIRT_BASE) {
        *(.data .data.*)
    }

    . = ALIGN(0x1000);
    .bss : AT(ADDR(.bss) - KERNEL_VIRT_BASE) {
        *(COMMON)
        *(.bss .bss.*)
        . = ALIGN(0x1000);
    }

    _kernel_virt_end = .;
    _kernel_phys_end = . - KERNEL_VIRT_BASE;

    /DISCARD/ : {
        *(.comment)
        *(.eh_frame)
        *(.note*)
    }
}
