#pragma once
#include "idt.h"

void pic_init(void);
void pic_eoi(uint8_t irq);
void irq_register(uint8_t irq, isr_handler_t handler);
void irq_unregister(uint8_t irq);
