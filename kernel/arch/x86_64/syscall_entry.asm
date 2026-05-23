; DevOS — SYSCALL Entry/Exit
;
; CPU state on SYSCALL entry:
;   CS/SS  = kernel selectors (from STAR)
;   RIP    = this label (from LSTAR)
;   RCX    = saved user RIP
;   R11    = saved user RFLAGS
;   RSP    = still user RSP
;   RAX    = syscall number
;   RDI/RSI/RDX/R10/R8/R9 = syscall args
;   IF=0 (cleared by SFMASK)
;
; Stack discipline:
;   1. Save user RSP to syscall_user_rsp (no stack use yet)
;   2. Load kernel RSP from syscall_kernel_rsp
;   3. Build syscall frame on kernel stack
;   4. Dispatch
;   5. Restore and SYSRET
;
; Syscall frame layout (after pushing, RSP at bottom):
;   [kernel_stack_top - 8]   user RIP  (rcx)
;   [kernel_stack_top - 16]  user RFLAGS (r11)
;   [kernel_stack_top - 24]  user RSP  (saved before switch)
;   [kernel_stack_top - 32]  r10  (arg4 in syscall ABI)
;   [kernel_stack_top - 40]  r9
;   [kernel_stack_top - 48]  r8
;   RSP here during dispatch
;
; C calling convention for dispatch:
;   arg1=rdi, arg2=rsi, arg3=rdx, arg4=rcx, arg5=r8, arg6=r9
;   syscall: arg1=rdi, arg2=rsi, arg3=rdx, arg4=r10, arg5=r8, arg6=r9
;   → move r10 into rcx before call

[BITS 64]
[SECTION .text]

global syscall_entry
extern syscall_table
extern syscall_kernel_rsp

syscall_entry:
    ; Interrupts are OFF (SFMASK cleared IF).
    ; RSP is user RSP — do not use it.

    ; Atomically save user RSP and switch to kernel RSP.
    ; We use a per-CPU scratch slot (pre-SMP: one global slot is fine).
    mov     [rel syscall_user_rsp], rsp
    mov     rsp, [rel syscall_kernel_rsp]

    ; Push context onto kernel stack (aligned down by 5 pushes = 40 bytes;
    ; kernel_stack_top is 16-byte aligned, so RSP is 16-byte aligned
    ; after an even number of 8-byte pushes, but C ABI requires RSP
    ; to be misaligned by 8 at the point of CALL (after CALL pushes
    ; return addr it becomes aligned). We push 6 × 8 = 48 bytes
    ; before CALL, so RSP is 16-byte aligned before CALL → correct.)
    push    rcx                 ; user RIP (saved by SYSCALL)
    push    r11                 ; user RFLAGS
    push    qword [rel syscall_user_rsp]  ; user RSP
    push    r10                 ; arg4 (syscall ABI uses r10, not rcx)
    push    r9
    push    r8

    ; Bounds-check syscall number
    cmp     rax, 512
    jae     .invalid

    ; Load handler from table
    lea     r11, [rel syscall_table]
    mov     r11, [r11 + rax * 8]
    test    r11, r11
    jz      .invalid

    ; Fix arg4: syscall uses r10; C uses rcx
    mov     rcx, r10

    ; Dispatch: rdi, rsi, rdx already correct; rcx=arg4, r8=arg5, r9=arg6
    ; Stack is 16-byte aligned before this call.
    call    r11

    jmp     .done

.invalid:
    mov     rax, -38            ; -ENOSYS

.done:
    ; Restore saved registers
    pop     r8
    pop     r9
    pop     r10
    pop     rcx                 ; was user RSP — don't need it here
    pop     r11                 ; user RFLAGS
    pop     rcx                 ; user RIP

    ; Restore user RSP
    mov     rsp, [rel syscall_user_rsp]

    ; Return to ring3
    ; SYSRET: RIP ← RCX, RFLAGS ← R11, CS/SS ← STAR[63:48]
    o64 sysret

[SECTION .data]
global syscall_user_rsp
syscall_user_rsp: dq 0
