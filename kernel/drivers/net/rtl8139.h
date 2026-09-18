/*
 * DevOS — RTL8139 NIC driver
 *
 * PCI vendor 0x10EC device 0x8139 (Realtek 8139C/8139D).
 * QEMU: -nic rtl8139,model=rtl8139
 *
 * Uses I/O port BAR0.  RX ring buffer 64 KB (continuous).
 * TX: 4 fixed descriptors (TSAD0–3 / TSD0–3).
 * IRQ registered via irq_register().
 */
#pragma once
#include "../../include/types.h"

bool rtl8139_init(void);
