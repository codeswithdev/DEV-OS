; DevOS Stage 2 — 2560 bytes max
; Loads kernel sectors from LBA 6, enters long mode, jumps to kernel.
; E820 memory map stored at BOOT_INFO (0x8000).

[BITS 16]
[ORG 0x7E00]

%define KERNEL_LBA      6
%define KERNEL_SECTORS  512
%define SCRATCH_SEG     0x0900  ; 0x9000 physical scratch for disk reads
%define BOOT_INFO       0x8000

; boot_info_t field offsets (matches C struct, pack=1)
%define BI_MAGIC        0
%define BI_E820_CNT     4
%define BI_E820_MAP     8       ; 32 × 24 bytes = 768 bytes (we use 20-byte entries padded)
%define BI_KPHYS_START  (8 + 32*24)
%define BI_KPHYS_END    (BI_KPHYS_START + 8)

start:
    cli
    xor     ax, ax
    mov     ds, ax
    mov     es, ax
    mov     ss, ax
    mov     sp, 0x7C00
    cld
    sti

    mov     [boot_drv], dl

    ; A20 via keyboard controller
    call    a20_enable

    ; Build boot_info
    mov     dword [BOOT_INFO + BI_MAGIC], 0xB007B007
    call    e820_detect

    ; Load kernel to 1MB physical using BIOS int13h + A20 copy trick
    call    load_kernel_1mb

    ; Minimal long-mode page tables at 0x2000
    call    setup_early_paging

    ; Enter long mode and jump to kernel
    call    enter_long_mode

    cli
.halt: hlt
    jmp .halt

; ---- A20 ----
a20_enable:
    ; Try port 0x92 fast A20
    in      al, 0x92
    or      al, 0x02
    and     al, 0xFE
    out     0x92, al
    ret

; ---- E820 ----
; Entries: base(8)+length(8)+type(4) = 20 bytes, padded to 24
e820_detect:
    xor     ebx, ebx
    mov     di, BOOT_INFO + BI_E820_MAP
    xor     esi, esi        ; count
.next:
    mov     eax, 0xE820
    mov     edx, 0x534D4150
    mov     ecx, 24
    int     0x15
    jc      .done
    cmp     eax, 0x534D4150
    jne     .done
    add     di, 24
    inc     esi
    cmp     esi, 32
    je      .done
    test    ebx, ebx
    jnz     .next
.done:
    mov     [BOOT_INFO + BI_E820_CNT], esi
    ret

; ---- Load kernel to 1MB using extended int13h ----
; We read 32 sectors (16KB) at a time to 0x9000 (scratch)
; then use rep movsb with 32-bit addressing to copy to >1MB dest
load_kernel_1mb:
    ; Switch to unreal mode for 32-bit segment access
    cli
    lgdt    [gdt_unreal_ptr]
    mov     eax, cr0
    or      al,  1
    mov     cr0, eax
    mov     bx,  8
    mov     ds,  bx
    mov     es,  bx
    and     al,  ~1
    mov     cr0, eax
    xor     ax,  ax
    mov     ds,  ax
    sti
    ; Now DS/ES have 4GB descriptor cached — unreal mode active

    mov     ebx, KERNEL_LBA     ; current LBA
    mov     edi, 0x100000       ; destination
    mov     ecx, KERNEL_SECTORS

.loop:
    test    ecx, ecx
    jz      .done

    ; sectors_this = min(ecx, 32)
    mov     eax, ecx
    cmp     eax, 32
    jle     .no_clamp
    mov     eax, 32
.no_clamp:
    ; Build DAP at 0x7000
    mov     word [0x7000],   0x0010   ; DAP size
    mov     word [0x7002],   0        ; reserved
    mov     word [0x7004],   ax       ; sectors
    mov     word [0x7006],   0x9000   ; buffer offset
    mov     word [0x7008],   0x0000   ; buffer segment
    mov     dword [0x700A],  ebx      ; LBA low
    mov     dword [0x700E],  0        ; LBA high

    push    ax
    mov     ah,  0x42
    mov     dl,  [boot_drv]
    mov     si,  0x7000
    int     0x13
    jc      load_err
    pop     ax

    ; Copy from 0x9000 to edi (32-bit, unreal mode)
    push    ecx
    mov     ecx, eax
    shl     ecx, 9              ; bytes = sectors * 512
    ; Use 32-bit address override prefix for movsd
    pushad
    mov     esi, 0x9000
    ; edi already set
    shr     ecx, 2
    a32 rep movsd
    popad
    pop     ecx

    ; Advance
    push    eax
    pop     edx
    add     ebx, edx
    shl     edx, 9
    add     edi, edx
    sub     ecx, [esp]          ; BUG: stack corrupt; fix:
    pop     edx                 ; get back original eax
    sub     ecx, edx
    jmp     .loop

.done:
    ; Store kphys range
    mov     dword [BOOT_INFO + BI_KPHYS_START], 0x100000
    mov     dword [BOOT_INFO + BI_KPHYS_START+4], 0
    mov     dword [BOOT_INFO + BI_KPHYS_END], 0x100000 + KERNEL_SECTORS*512
    mov     dword [BOOT_INFO + BI_KPHYS_END+4], 0
    ret

load_err:
    mov     si, s_err
.lp: lodsb
    test al, al
    jz .h
    mov ah, 0x0E
    int 0x10
    jmp .lp
.h: cli
    hlt

; ---- Early page tables ----
; PML4 @ 0x2000, PDPT @ 0x3000, PD @ 0x4000
; Maps 0..1GB identity (2MB pages)
; Maps 0xFFFFFFFF80000000..+512MB same PD
setup_early_paging:
    cli
    ; Zero 3 pages
    xor     eax, eax
    mov     edi, 0x2000
    mov     ecx, 3*4096/4
    a32 rep stosd

    ; PML4[0] = 0x3000 | 3  (identity)
    mov     dword [0x2000], 0x3003

    ; PML4[511] = 0x3000 | 3  (upper canonical)
    ; Entry 511 index = 511*8 = 4088
    mov     dword [0x2000 + 511*8], 0x3003

    ; PDPT[0] = 0x4000|3  (0..1GB)
    mov     dword [0x3000], 0x4003

    ; PDPT[510] = 0x4000|3  (0xFFFFFFFF80000000 maps PDPT index 510)
    ; virt 0xFFFFFFFF80000000: PML4[511], PDPT[510], ...
    mov     dword [0x3000 + 510*8], 0x4003

    ; PD: 2MB pages covering 0..1GB
    mov     edi, 0x4000
    xor     ebx, ebx
.fill:
    mov     dword [edi],   ebx
    or      dword [edi],   0x83    ; P+RW+PS (2MB)
    mov     dword [edi+4], 0
    add     edi, 8
    add     ebx, 0x200000
    cmp     ebx, 0x40000000       ; 1GB
    jl      .fill

    ret

; ---- Enter long mode ----
enter_long_mode:
    cli

    lgdt    [gdt64_ptr]

    ; PAE
    mov     eax, cr4
    or      eax, (1<<5)
    mov     cr4, eax

    ; CR3
    mov     eax, 0x2000
    mov     cr3, eax

    ; EFER.LME
    mov     ecx, 0xC0000080
    rdmsr
    or      eax, (1<<8)
    wrmsr

    ; Enable paging + PM
    mov     eax, cr0
    or      eax, (1<<31)|1
    mov     cr0, eax

    ; Far jump to 64-bit segment
    jmp     0x08:lm_entry

[BITS 64]
lm_entry:
    mov     ax,  0x10
    mov     ds,  ax
    mov     es,  ax
    mov     ss,  ax
    xor     ax,  ax
    mov     fs,  ax
    mov     gs,  ax
    mov     rsp, 0x90000

    mov     rdi, BOOT_INFO
    ; Kernel is loaded at physical 0x100000.
    ; Jump there (identity mapped).
    mov     rax, 0x100000
    jmp     rax

[BITS 16]

boot_drv:   db 0x80
s_err:      db "Stage2 disk err", 0

align 8
gdt_unreal:
    dq 0
    dq 0x00CF92000000FFFF   ; 32-bit data 4GB
gdt_unreal_ptr:
    dw $ - gdt_unreal - 1
    dd gdt_unreal

align 8
gdt64:
    dq 0                        ; null
    dq 0x00AF9A000000FFFF       ; 0x08 kernel code 64
    dq 0x00AF92000000FFFF       ; 0x10 kernel data
gdt64_ptr:
    dw $ - gdt64 - 1
    dd gdt64
    dd 0
