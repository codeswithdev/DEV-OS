; DevOS userspace test
; Built as ELF64, linked at 0x400000

[BITS 64]

section .text
global _start

_start:
    ; sys_write(1, msg_hello, len_hello)
    mov     rax, 1
    mov     rdi, 1
    lea     rsi, [rel msg_hello]
    mov     rdx, len_hello
    syscall

    ; sys_getpid → rax
    mov     rax, 39
    syscall

    ; Convert low digit to ASCII
    and     rax, 0xF
    add     rax, '0'
    mov     [rel digit_buf], al

    ; Write "[USER] PID=X\n"
    mov     rax, 1
    mov     rdi, 1
    lea     rsi, [rel msg_pid]
    mov     rdx, len_pid
    syscall

    ; sched_yield
    mov     rax, 24
    syscall

    ; Write done
    mov     rax, 1
    mov     rdi, 1
    lea     rsi, [rel msg_done]
    mov     rdx, len_done
    syscall

    ; exit(0)
    mov     rax, 60
    xor     rdi, rdi
    syscall
    hlt

section .rodata
msg_hello:  db  "[USER] Hello from ring3! DevOS v0.3", 10
len_hello   equ $ - msg_hello
msg_done:   db  "[USER] Done. Exiting.", 10
len_done    equ $ - msg_done

section .data
msg_pid:    db  "[USER] PID="
digit_buf:  db  '?', 10
len_pid     equ $ - msg_pid
