; DevOS Stage 1 — MBR (512 bytes)
; Loads stage2 from LBA 1..5 (5 sectors = 2560 bytes) to 0x7E00
; Then jumps to stage2.

[BITS 16]
[ORG 0x7C00]

start:
    cli
    xor     ax, ax
    mov     ds, ax
    mov     es, ax
    mov     ss, ax
    mov     sp, 0x7C00
    sti

    ; Save boot drive
    mov     [boot_drive], dl

    ; Load stage2: 5 sectors from LBA 1 → 0x0000:0x7E00
    mov     ah, 0x42        ; Extended Read
    mov     dl, [boot_drive]
    mov     si, dap
    int     0x13
    jc      disk_error

    jmp     0x0000:0x7E00

disk_error:
    mov     si, err_msg
.loop:
    lodsb
    test    al, al
    jz      .hang
    mov     ah, 0x0E
    int     0x10
    jmp     .loop
.hang:
    cli
    hlt

boot_drive: db 0

; Disk Address Packet
align 4
dap:
    db  0x10        ; size of DAP
    db  0x00        ; reserved
    dw  5           ; sectors to read (stage2 ≤ 2560 bytes)
    dw  0x7E00      ; offset
    dw  0x0000      ; segment
    dq  1           ; LBA start

err_msg: db "Stage1 disk error", 0

times 510 - ($ - $$) db 0
dw 0xAA55
