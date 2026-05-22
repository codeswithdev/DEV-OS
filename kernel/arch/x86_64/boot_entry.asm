; DevOS Kernel Entry Point
; CPU is in 64-bit long mode, paging = stage2 early tables (2MB pages).
; RDI = physical address of boot_info_t.
; We are executing at physical 0x100000, but linked at KERNEL_VIRT_BASE+0x100000.
; Stage2 identity maps all physical memory, so both addresses work.
;
; Steps:
;   1. Adjust RDI (boot_info phys → virt via identity map, stays same value)
;   2. Switch to a proper kernel stack in BSS
;   3. Call kmain(boot_info_t*)

[BITS 64]
[SECTION .boot_entry]  ; placed at very start of kernel binary by linker

KERNEL_PHYS_BASE    equ 0x100000
KERNEL_VIRT_BASE    equ 0xFFFFFFFF80000000

global _start
extern kmain
extern _kernel_phys_end

_start:
    ; Disable interrupts (may already be off)
    cli

    ; At this point RDI = boot_info physical address (valid as-is, identity mapped)
    ; Save it across the stack switch
    mov     rsi, rdi    ; save boot_info ptr

    ; Set up kernel stack: use boot_stack defined in BSS below
    ; The linker puts us at VIRT_BASE+phys, but we're running at identity phys.
    ; early_stack_top is a linked virtual address; we compute its physical:
    ;   phys = virt - KERNEL_VIRT_BASE + KERNEL_PHYS_BASE
    ; But early page tables map virt directly from stage2.
    ; Actually: stage2 PML4[511]→PDPT[510]→PD covers 0xFFFFFFFF80000000.
    ; So our linked virtual addresses WILL work once we do a far jump or just
    ; use them. The CPU is fetching instructions at physical 0x100000 via the
    ; identity map, but our symbols are linked at virtual VIRT_BASE+0x100000.
    ; Both map to the same physical page — this works.
    ;
    ; Use the virtual stack address directly:
    lea     rsp, [rel early_stack_top]

    ; Restore boot_info and call kmain
    mov     rdi, rsi
    call    kmain

    ; kmain should not return
    cli
.hang:
    hlt
    jmp .hang

[SECTION .bss]
align 16
early_stack:    resb 16384  ; 16KB early kernel stack
early_stack_top:
