/*
 * DevOS — Local APIC driver (apic.h)
 * x86_64 xAPIC via MMIO at 0xFEE00000 (default)
 */
#pragma once
#include "../include/types.h"

bool apic_init(void);       /* returns false if APIC not available */
void apic_eoi(void);        /* send End-Of-Interrupt */
uint32_t apic_id(void);     /* return local APIC ID of current CPU */
bool apic_available(void);  /* true if APIC was successfully initialised */
