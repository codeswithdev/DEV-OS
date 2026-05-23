; DevOS — Ring3 Entry Trampoline
;
; ring3_enter_first:
;   Called by sched_switch 'ret' for user tasks on their first dispatch.
;   current_task is already set to the user task.
;   Builds an iretq frame and returns to ring3.
;
; iretq frame layout (pushed in order, top of stack = last pushed):
;   [rsp+32]  SS      (user data selector | RPL3)
;   [rsp+24]  RSP     (user stack pointer)
;   [rsp+16]  RFLAGS  (IF=1, IOPL=0)
;   [rsp+8]   CS      (user code selector | RPL3)
;   [rsp+0]   RIP     (user entry point)
;
; Selectors (from gdt.h):
;   SEL_USER_CODE = (GDT_USER_CODE << 3) | 3 = (4*8)|3 = 0x23
;   SEL_USER_DATA = (GDT_USER_DATA << 3) | 3 = (3*8)|3 = 0x1B
;
; IMPORTANT: After GDT fix, user_data is at index 3 (0x18|3=0x1B)
;            and user_code is at index 4 (0x20|3=0x23).
; These constants must match gdt.h exactly.

[BITS 64]
[SECTION .text]

global ring3_enter_first
extern current_task

%define SEL_USER_CODE   0x23    ; GDT index 4, RPL=3
%define SEL_USER_DATA   0x1B    ; GDT index 3, RPL=3

; task_t field offsets (must match sched.h)
%define TASK_RIP_ENTRY   112    ; rip_entry
%define TASK_USER_STACK  104    ; user_stack
%define TASK_KSTACK      96     ; kernel_stack

ring3_enter_first:
    ; Load current_task pointer
    mov     rax, [rel current_task]

    ; Load user entry RIP and RSP from task
    mov     rcx, [rax + TASK_RIP_ENTRY]   ; user RIP
    mov     rdx, [rax + TASK_USER_STACK]  ; user RSP

    ; Zero all general-purpose registers before ring3
    ; (prevent kernel pointer leakage)
    xor     rax, rax
    xor     rbx, rbx
    xor     rsi, rsi
    xor     rdi, rdi
    xor     r8,  r8
    xor     r9,  r9
    xor     r10, r10
    xor     r11, r11
    xor     r12, r12
    xor     r13, r13
    xor     r14, r14
    xor     r15, r15
    xor     rbp, rbp

    ; Build iretq frame on current kernel stack
    ; Stack must be 16-byte aligned before iretq (after 5 pushes = 40 bytes;
    ; add 8 if RSP was 16-byte aligned before pushes to maintain alignment)
    push    qword SEL_USER_DATA  ; SS
    push    rdx                  ; user RSP
    push    qword 0x202          ; RFLAGS: IF=1, reserved bit 1 set
    push    qword SEL_USER_CODE  ; CS
    push    rcx                  ; RIP

    ; Load user data segment selectors before iretq
    ; (DS/ES/FS/GS must be user-accessible or null in ring3)
    mov     ax, SEL_USER_DATA
    mov     ds, ax
    mov     es, ax
    mov     fs, ax
    mov     gs, ax

    iretq
