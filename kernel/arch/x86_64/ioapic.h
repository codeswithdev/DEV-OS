/*
 * DevOS — I/O APIC driver (ioapic.h)
 */
#pragma once
#include "../include/types.h"

bool ioapic_init(void);
void ioapic_set_irq(uint8_t irq, uint8_t vector, uint8_t dest_apic_id, bool level, bool active_low);
void ioapic_mask_irq(uint8_t irq);
void ioapic_unmask_irq(uint8_t irq);
