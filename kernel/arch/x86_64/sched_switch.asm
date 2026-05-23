; DevOS — Context Switch
; void sched_switch(task_t *from, task_t *to)
;   rdi = from, rsi = to
;
; task_t layout offsets (must match sched.h):
;   cpu_state_t @ 40:  r15(0),r14(8),r13(16),r12(24),rbx(32),rbp(40),rsp(48)
;   kernel_stack @ 96
;   cr3 @ 120

[BITS 64]
[SECTION .text]

global sched_switch

%define OFF_R15  40
%define OFF_R14  48
%define OFF_R13  56
%define OFF_R12  64
%define OFF_RBX  72
%define OFF_RBP  80
%define OFF_RSP  88
%define OFF_KSTK 96
%define OFF_CR3  120

sched_switch:
    ; Save callee-saved regs into from->cpu
    mov     [rdi + OFF_R15], r15
    mov     [rdi + OFF_R14], r14
    mov     [rdi + OFF_R13], r13
    mov     [rdi + OFF_R12], r12
    mov     [rdi + OFF_RBX], rbx
    mov     [rdi + OFF_RBP], rbp
    mov     [rdi + OFF_RSP], rsp

    ; Switch CR3 if needed
    mov     rax, [rdi + OFF_CR3]
    mov     rcx, [rsi + OFF_CR3]
    cmp     rax, rcx
    je      .same_cr3
    mov     cr3, rcx
.same_cr3:

    ; Restore from to->cpu
    mov     r15, [rsi + OFF_R15]
    mov     r14, [rsi + OFF_R14]
    mov     r13, [rsi + OFF_R13]
    mov     r12, [rsi + OFF_R12]
    mov     rbx, [rsi + OFF_RBX]
    mov     rbp, [rsi + OFF_RBP]
    mov     rsp, [rsi + OFF_RSP]

    ; TSS RSP0 is updated by C code in sched_do_switch before calling us.
    ; No redundant call to gdt_set_kernel_stack here.
    ret
