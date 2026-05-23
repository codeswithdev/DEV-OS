; DevOS — IDT stubs
; 256 vectors: 0-31 exceptions, 32-47 IRQs (PIC), 48+ software

[BITS 64]
[SECTION .text]

extern isr_dispatch

; Macro for exceptions WITHOUT error code
%macro ISR_NOERR 1
isr_stub_%1:
    push    qword 0         ; dummy error
    push    qword %1
    jmp     isr_common
%endmacro

; Macro for exceptions WITH error code (CPU already pushed it)
%macro ISR_ERR 1
isr_stub_%1:
    push    qword %1
    jmp     isr_common
%endmacro

; Exception stubs 0-31
ISR_NOERR 0
ISR_NOERR 1
ISR_NOERR 2
ISR_NOERR 3
ISR_NOERR 4
ISR_NOERR 5
ISR_NOERR 6
ISR_NOERR 7
ISR_ERR   8
ISR_NOERR 9
ISR_ERR   10
ISR_ERR   11
ISR_ERR   12
ISR_ERR   13
ISR_ERR   14
ISR_NOERR 15
ISR_NOERR 16
ISR_ERR   17
ISR_NOERR 18
ISR_NOERR 19
ISR_NOERR 20
ISR_NOERR 21
ISR_NOERR 22
ISR_NOERR 23
ISR_NOERR 24
ISR_NOERR 25
ISR_NOERR 26
ISR_NOERR 27
ISR_NOERR 28
ISR_NOERR 29
ISR_ERR   30
ISR_NOERR 31

; IRQ stubs 32-47
%assign i 32
%rep 16
ISR_NOERR i
%assign i i+1
%endrep

; Generic stubs 48-255
%assign i 48
%rep 208
ISR_NOERR i
%assign i i+1
%endrep

isr_common:
    ; At entry: [rsp] = vec, [rsp+8] = err (or dummy), then iretq frame
    ; Push all GP registers
    push    rax
    push    rbx
    push    rcx
    push    rdx
    push    rsi
    push    rdi
    push    rbp
    push    r8
    push    r9
    push    r10
    push    r11
    push    r12
    push    r13
    push    r14
    push    r15

    mov     rdi, rsp        ; interrupt_frame_t *
    call    isr_dispatch

    pop     r15
    pop     r14
    pop     r13
    pop     r12
    pop     r11
    pop     r10
    pop     r9
    pop     r8
    pop     rbp
    pop     rdi
    pop     rsi
    pop     rdx
    pop     rcx
    pop     rbx
    pop     rax
    add     rsp, 16         ; pop vec and err
    iretq

; Export stub table for idt.c
global isr_stub_table
isr_stub_table:
%assign i 0
%rep 256
    dq isr_stub_%+i
%assign i i+1
%endrep
