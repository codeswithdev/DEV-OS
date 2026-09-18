/*
 * DevOS — ACPI subsystem (acpi.h)
 * Parses RSDP -> RSDT/XSDT -> MADT to discover APIC/IOAPIC topology.
 */
#pragma once
#include "../include/types.h"

/* ---- ACPI MADT entry types ---- */
#define MADT_LAPIC          0
#define MADT_IOAPIC         1
#define MADT_ISO            2   /* Interrupt Source Override */
#define MADT_NMI_SRC        3
#define MADT_LAPIC_NMI      4
#define MADT_LAPIC_ADDR_OVR 5
#define MADT_X2APIC         9

typedef struct {
    uint32_t lapic_phys;            /* Physical address of Local APIC MMIO */
    bool     has_8259;              /* Dual 8259 PICs present */

    /* Up to 16 I/O APICs */
    uint8_t  ioapic_count;
    struct {
        uint8_t  id;
        uint32_t phys;              /* IOAPIC MMIO base */
        uint32_t gsi_base;          /* Global System Interrupt base */
    } ioapic[16];

    /* Interrupt source overrides (remaps ISA IRQs) */
    uint8_t  iso_count;
    struct {
        uint8_t  bus;
        uint8_t  irq;
        uint32_t gsi;
        uint16_t flags;
    } iso[32];

    /* Local APIC IDs of detected CPUs */
    uint8_t  cpu_count;
    uint8_t  cpu_lapic_id[64];
} acpi_info_t;

extern acpi_info_t g_acpi;

bool acpi_init(void);
void *acpi_find_table(const char sig[4]);
void acpi_shutdown(void);
void acpi_reboot(void);
