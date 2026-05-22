#pragma once
#include "../../include/types.h"

/* Interrupt frame pushed by CPU + our stubs */
typedef struct {
    uint64_t r15, r14, r13, r12, r11, r10, r9, r8;
    uint64_t rbp, rdi, rsi, rdx, rcx, rbx, rax;
    uint64_t vec, err;
    uint64_t rip, cs, rflags, rsp, ss;
} PACKED interrupt_frame_t;

typedef void (*isr_handler_t)(interrupt_frame_t *frame);

void idt_init(void);
void idt_set_handler(uint8_t vec, isr_handler_t handler);
