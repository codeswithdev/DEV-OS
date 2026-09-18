/*
 * DevOS — I/O APIC driver
 *
 * The I/O APIC routes hardware interrupts to Local APICs.
 * It is discovered via the ACPI MADT (g_acpi.ioapic[]).
 *
 * Registers are accessed via an indirect scheme:
 *   IOREGSEL (offset 0x00) — write the register index
 *   IOWIN    (offset 0x10) — read/write data
 *
 * Redirection table entries (RTE) are 64-bit:
 *   [7:0]   interrupt vector
 *   [10:8]  delivery mode (000=fixed)
 *   [11]    destination mode (0=physical)
 *   [12]    delivery status (read-only)
 *   [13]    pin polarity (0=active high, 1=active low)
 *   [14]    remote IRR (read-only)
 *   [15]    trigger mode (0=edge, 1=level)
 *   [16]    mask (1=masked)
 *   [63:56] destination (APIC ID in physical mode)
 */
#include "ioapic.h"
#include "serial.h"
#include "../../acpi/acpi.h"
#include "../../mm/vmm.h"
#include "../../include/types.h"

#define IOAPIC_REGSEL   0x00
#define IOAPIC_WIN      0x10

#define IOAPIC_ID_REG   0x00
#define IOAPIC_VER_REG  0x01
#define IOAPIC_RTE_BASE 0x10    /* RTE for IRQ n = 0x10 + 2*n */

typedef struct {
    volatile uint32_t *base;
    uint32_t           gsi_base;
    uint8_t            max_rte;    /* number of redirection entries */
    bool               valid;
} ioapic_t;

#define MAX_IOAPICS 16
static ioapic_t ioapics[MAX_IOAPICS];
static int      ioapic_count = 0;

static uint32_t ioapic_read(ioapic_t *io, uint8_t reg)
{
    io->base[IOAPIC_REGSEL >> 2] = reg;
    return io->base[IOAPIC_WIN  >> 2];
}

static void ioapic_write(ioapic_t *io, uint8_t reg, uint32_t val)
{
    io->base[IOAPIC_REGSEL >> 2] = reg;
    io->base[IOAPIC_WIN    >> 2] = val;
}

static void ioapic_write_rte(ioapic_t *io, uint8_t irq, uint64_t rte)
{
    uint8_t reg = (uint8_t)(IOAPIC_RTE_BASE + 2 * irq);
    ioapic_write(io, reg,     (uint32_t)(rte & 0xFFFFFFFF));
    ioapic_write(io, reg + 1, (uint32_t)(rte >> 32));
}

static uint64_t ioapic_read_rte(ioapic_t *io, uint8_t irq)
{
    uint8_t reg = (uint8_t)(IOAPIC_RTE_BASE + 2 * irq);
    uint64_t lo = ioapic_read(io, reg);
    uint64_t hi = ioapic_read(io, reg + 1);
    return lo | (hi << 32);
}

/* Find the IOAPIC responsible for a given global system interrupt */
static ioapic_t *ioapic_for_gsi(uint32_t gsi, uint8_t *local_irq)
{
    for (int i = 0; i < ioapic_count; i++) {
        if (!ioapics[i].valid) continue;
        uint32_t base = ioapics[i].gsi_base;
        if (gsi >= base && gsi < base + ioapics[i].max_rte) {
            *local_irq = (uint8_t)(gsi - base);
            return &ioapics[i];
        }
    }
    return NULL;
}

bool ioapic_init(void)
{
    if (g_acpi.ioapic_count == 0) {
        serial_puts("[IOAPIC] none found in ACPI MADT\n");
        return false;
    }

    for (uint8_t i = 0; i < g_acpi.ioapic_count && i < MAX_IOAPICS; i++) {
        uint64_t phys = g_acpi.ioapic[i].phys;
        uint64_t virt = KERNEL_VIRT_BASE + phys;

        vmm_map(kernel_pml4, virt, phys, VMM_MMIO);

        ioapics[i].base     = (volatile uint32_t *)(uintptr_t)virt;
        ioapics[i].gsi_base = g_acpi.ioapic[i].gsi_base;
        ioapics[i].valid    = true;

        uint32_t ver = ioapic_read(&ioapics[i], IOAPIC_VER_REG);
        ioapics[i].max_rte = (uint8_t)(((ver >> 16) & 0xFF) + 1);

        ioapic_count++;

        serial_printf("[IOAPIC] #%u phys=0x%x gsi_base=%u rte=%u\n",
                      i, g_acpi.ioapic[i].phys,
                      ioapics[i].gsi_base, ioapics[i].max_rte);

        /* Mask all entries initially */
        for (uint8_t j = 0; j < ioapics[i].max_rte; j++) {
            uint64_t rte = ioapic_read_rte(&ioapics[i], j);
            rte |= (1ULL << 16);  /* set mask bit */
            ioapic_write_rte(&ioapics[i], j, rte);
        }
    }

    serial_printf("[IOAPIC] init OK (%d I/O APIC(s))\n", ioapic_count);
    return true;
}

void ioapic_set_irq(uint8_t irq, uint8_t vector, uint8_t dest_apic_id,
                    bool level, bool active_low)
{
    uint32_t gsi = irq;

    /* Apply interrupt source overrides from ACPI */
    for (uint8_t i = 0; i < g_acpi.iso_count; i++) {
        if (g_acpi.iso[i].irq == irq) {
            gsi        = g_acpi.iso[i].gsi;
            uint16_t f = g_acpi.iso[i].flags;
            if ((f & 0x3) == 3) active_low = true;
            if ((f & 0xC) >> 2 == 3) level = true;
            break;
        }
    }

    uint8_t   local_irq;
    ioapic_t *io = ioapic_for_gsi(gsi, &local_irq);
    if (!io) {
        serial_printf("[IOAPIC] no IOAPIC for GSI %u\n", gsi);
        return;
    }

    uint64_t rte = vector;
    if (active_low) rte |= (1ULL << 13);
    if (level)      rte |= (1ULL << 15);
    rte |= ((uint64_t)dest_apic_id << 56);
    /* Unmask */
    rte &= ~(1ULL << 16);

    ioapic_write_rte(io, local_irq, rte);
    serial_printf("[IOAPIC] IRQ%u -> GSI%u -> vec 0x%02x\n", irq, gsi, vector);
}

void ioapic_mask_irq(uint8_t irq)
{
    uint8_t   local_irq;
    ioapic_t *io = ioapic_for_gsi(irq, &local_irq);
    if (!io) return;
    uint64_t rte = ioapic_read_rte(io, local_irq);
    rte |= (1ULL << 16);
    ioapic_write_rte(io, local_irq, rte);
}

void ioapic_unmask_irq(uint8_t irq)
{
    uint8_t   local_irq;
    ioapic_t *io = ioapic_for_gsi(irq, &local_irq);
    if (!io) return;
    uint64_t rte = ioapic_read_rte(io, local_irq);
    rte &= ~(1ULL << 16);
    ioapic_write_rte(io, local_irq, rte);
}
