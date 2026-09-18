/*
 * DevOS — ACPI subsystem
 *
 * Implements:
 *   - RSDP search (EBDA + BIOS ROM area)
 *   - RSDP checksum validation
 *   - RSDT / XSDT parsing
 *   - MADT parsing (CPUs, I/O APICs, interrupt source overrides)
 *   - Lightweight acpi_find_table() for drivers that need ACPI tables
 *   - QEMU-compatible shutdown (port 0x604) and reboot via FADT/ResetReg
 */
#include "acpi.h"
#include "../mm/vmm.h"
#include "../lib/string.h"
#include "../arch/x86_64/serial.h"

/* ---- Global ACPI info, populated by acpi_init() ---- */
acpi_info_t g_acpi;

/* ---- Standard ACPI table header (8-byte signature + length + ...) ---- */
typedef struct {
    char     signature[4];
    uint32_t length;
    uint8_t  revision;
    uint8_t  checksum;
    char     oem_id[6];
    char     oem_table_id[8];
    uint32_t oem_revision;
    uint32_t creator_id;
    uint32_t creator_revision;
} PACKED acpi_header_t;

/* ---- RSDP ---- */
typedef struct {
    char     signature[8];  /* "RSD PTR " */
    uint8_t  checksum;
    char     oem_id[6];
    uint8_t  revision;
    uint32_t rsdt_address;
    /* ACPI 2.0+ */
    uint32_t length;
    uint64_t xsdt_address;
    uint8_t  ext_checksum;
    uint8_t  reserved[3];
} PACKED rsdp_t;

/* ---- MADT ---- */
typedef struct {
    acpi_header_t hdr;
    uint32_t      lapic_addr;
    uint32_t      flags;
} PACKED madt_t;

typedef struct {
    uint8_t type;
    uint8_t length;
} PACKED madt_entry_t;

typedef struct {
    madt_entry_t hdr;
    uint8_t  acpi_cpu_uid;
    uint8_t  apic_id;
    uint32_t flags;
} PACKED madt_lapic_t;

typedef struct {
    madt_entry_t hdr;
    uint8_t  ioapic_id;
    uint8_t  reserved;
    uint32_t ioapic_addr;
    uint32_t gsi_base;
} PACKED madt_ioapic_t;

typedef struct {
    madt_entry_t hdr;
    uint8_t  bus;
    uint8_t  irq;
    uint32_t gsi;
    uint16_t flags;
} PACKED madt_iso_t;

/* ---- FADT (for reset register) ---- */
typedef struct {
    acpi_header_t hdr;
    uint32_t      firmware_ctrl;
    uint32_t      dsdt;
    uint8_t       reserved1;
    uint8_t       preferred_pm_profile;
    uint16_t      sci_int;
    uint32_t      smi_cmd;
    uint8_t       acpi_enable;
    uint8_t       acpi_disable;
    uint8_t       s4bios_req;
    uint8_t       pstate_ctrl;
    uint32_t      pm1a_evt_blk;
    uint32_t      pm1b_evt_blk;
    uint32_t      pm1a_cnt_blk;
    uint32_t      pm1b_cnt_blk;
    uint32_t      pm2_cnt_blk;
    uint32_t      pm_tmr_blk;
    uint32_t      gpe0_blk;
    uint32_t      gpe1_blk;
    uint8_t       pm1_evt_len;
    uint8_t       pm1_cnt_len;
    uint8_t       pm2_cnt_len;
    uint8_t       pm_tmr_len;
    uint8_t       gpe0_blk_len;
    uint8_t       gpe1_blk_len;
    uint8_t       gpe1_base;
    uint8_t       cst_cnt;
    uint16_t      p_lvl2_lat;
    uint16_t      p_lvl3_lat;
    uint16_t      flush_size;
    uint16_t      flush_stride;
    uint8_t       duty_offset;
    uint8_t       duty_width;
    uint8_t       day_alrm;
    uint8_t       mon_alrm;
    uint8_t       century;
    uint16_t      iapc_boot_arch;
    uint8_t       reserved2;
    uint32_t      flags2;
    /* Gas structures + more — omitted, we only need pm1a_cnt_blk */
} PACKED fadt_t;

/* ---- I/O helpers ---- */
static inline void outb(uint16_t port, uint8_t v)
{
    __asm__ __volatile__("outb %0,%1" :: "a"(v), "Nd"(port) : "memory");
}
static inline void outw(uint16_t port, uint16_t v)
{
    __asm__ __volatile__("outw %0,%1" :: "a"(v), "Nd"(port) : "memory");
}

/* ---- Physical → kernel virtual (identity-mapped or offset) ---- */
static inline void *phys_to_virt(uint64_t phys)
{
    /* Kernel is at -2GB. Low physical memory is identity mapped in our VMM
     * setup, so both phys and phys + KERNEL_VIRT_BASE may work depending
     * on what vmm_init() set up. We try the direct KERNEL_VIRT_BASE offset
     * for addresses below 4 GB. */
    if (phys < 0x100000000ULL)
        return (void *)(uintptr_t)(phys + KERNEL_VIRT_BASE);
    return (void *)(uintptr_t)phys;
}

/* ---- Checksum validation ---- */
static bool checksum_valid(const void *p, size_t len)
{
    const uint8_t *b = (const uint8_t *)p;
    uint8_t sum = 0;
    for (size_t i = 0; i < len; i++) sum += b[i];
    return sum == 0;
}

/* ---- RSDP search ---- */
static rsdp_t *find_rsdp(void)
{
    /* Search EBDA (first 1 KB at the EBDA base) */
    uint16_t ebda_seg = *(uint16_t *)phys_to_virt(0x40E);
    uint64_t ebda_phys = (uint64_t)ebda_seg << 4;

    /* Scan EBDA first 1 KB */
    for (uint64_t addr = ebda_phys; addr < ebda_phys + 1024; addr += 16) {
        rsdp_t *r = (rsdp_t *)phys_to_virt(addr);
        if (memcmp(r->signature, "RSD PTR ", 8) == 0 &&
            checksum_valid(r, 20)) {
            return r;
        }
    }

    /* Search BIOS ROM area 0xE0000 – 0xFFFFF */
    for (uint64_t addr = 0xE0000; addr < 0x100000; addr += 16) {
        rsdp_t *r = (rsdp_t *)phys_to_virt(addr);
        if (memcmp(r->signature, "RSD PTR ", 8) == 0 &&
            checksum_valid(r, 20)) {
            return r;
        }
    }
    return NULL;
}

/* ---- Table scanning ---- */
/* Pointer to the root table (RSDT or XSDT), and whether it's 64-bit */
static void    *g_root_table = NULL;
static uint32_t g_root_entry_size = 4;  /* 4 for RSDT, 8 for XSDT */
static uint32_t g_root_entry_count = 0;

void *acpi_find_table(const char sig[4])
{
    if (!g_root_table) return NULL;
    acpi_header_t *hdr = (acpi_header_t *)g_root_table;
    uint8_t *entries = (uint8_t *)(hdr + 1);

    for (uint32_t i = 0; i < g_root_entry_count; i++) {
        uint64_t phys = 0;
        if (g_root_entry_size == 4) {
            phys = *(uint32_t *)(entries + i * 4);
        } else {
            phys = *(uint64_t *)(entries + i * 8);
        }
        acpi_header_t *t = (acpi_header_t *)phys_to_virt(phys);
        if (memcmp(t->signature, sig, 4) == 0 &&
            checksum_valid(t, t->length)) {
            return t;
        }
    }
    return NULL;
}

/* ---- MADT parsing ---- */
static void parse_madt(madt_t *madt)
{
    g_acpi.lapic_phys = madt->lapic_addr;
    g_acpi.has_8259   = (madt->flags & 1) != 0;

    uint8_t *ptr = (uint8_t *)(madt + 1);
    uint8_t *end = (uint8_t *)madt + madt->hdr.length;

    while (ptr < end) {
        madt_entry_t *e = (madt_entry_t *)ptr;
        if (e->length == 0) break;

        switch (e->type) {
        case MADT_LAPIC: {
            madt_lapic_t *cpu = (madt_lapic_t *)e;
            if ((cpu->flags & 1) && g_acpi.cpu_count < 64) {
                g_acpi.cpu_lapic_id[g_acpi.cpu_count++] = cpu->apic_id;
                serial_printf("[ACPI] CPU#%u lapic_id=%u\n",
                              g_acpi.cpu_count - 1, cpu->apic_id);
            }
            break;
        }
        case MADT_IOAPIC: {
            madt_ioapic_t *io = (madt_ioapic_t *)e;
            if (g_acpi.ioapic_count < 16) {
                int idx = g_acpi.ioapic_count++;
                g_acpi.ioapic[idx].id       = io->ioapic_id;
                g_acpi.ioapic[idx].phys     = io->ioapic_addr;
                g_acpi.ioapic[idx].gsi_base = io->gsi_base;
                serial_printf("[ACPI] IOAPIC#%d id=%u phys=0x%x gsi_base=%u\n",
                              idx, io->ioapic_id, io->ioapic_addr, io->gsi_base);
            }
            break;
        }
        case MADT_ISO: {
            madt_iso_t *iso = (madt_iso_t *)e;
            if (g_acpi.iso_count < 32) {
                int idx = g_acpi.iso_count++;
                g_acpi.iso[idx].bus   = iso->bus;
                g_acpi.iso[idx].irq   = iso->irq;
                g_acpi.iso[idx].gsi   = iso->gsi;
                g_acpi.iso[idx].flags = iso->flags;
                serial_printf("[ACPI] ISO bus=%u irq=%u -> gsi=%u\n",
                              iso->bus, iso->irq, iso->gsi);
            }
            break;
        }
        default: break;
        }
        ptr += e->length;
    }
}

/* ---- Public API ---- */

bool acpi_init(void)
{
    memset(&g_acpi, 0, sizeof(g_acpi));

    rsdp_t *rsdp = find_rsdp();
    if (!rsdp) {
        serial_puts("[ACPI] RSDP not found\n");
        return false;
    }
    serial_printf("[ACPI] RSDP found at 0x%p rev=%u\n",
                  (void *)rsdp, rsdp->revision);

    if (rsdp->revision >= 2 && rsdp->xsdt_address) {
        /* Use XSDT (64-bit pointers) */
        acpi_header_t *xsdt = (acpi_header_t *)phys_to_virt(rsdp->xsdt_address);
        if (checksum_valid(xsdt, xsdt->length)) {
            g_root_table       = xsdt;
            g_root_entry_size  = 8;
            g_root_entry_count = (xsdt->length - sizeof(acpi_header_t)) / 8;
            serial_printf("[ACPI] XSDT at 0x%p entries=%u\n",
                          (void *)xsdt, g_root_entry_count);
        }
    }

    if (!g_root_table) {
        /* Fall back to RSDT (32-bit pointers) */
        acpi_header_t *rsdt = (acpi_header_t *)phys_to_virt(rsdp->rsdt_address);
        if (checksum_valid(rsdt, rsdt->length)) {
            g_root_table       = rsdt;
            g_root_entry_size  = 4;
            g_root_entry_count = (rsdt->length - sizeof(acpi_header_t)) / 4;
            serial_printf("[ACPI] RSDT at 0x%p entries=%u\n",
                          (void *)rsdt, g_root_entry_count);
        } else {
            serial_puts("[ACPI] RSDT checksum invalid\n");
            return false;
        }
    }

    /* Parse MADT */
    madt_t *madt = (madt_t *)acpi_find_table("APIC");
    if (madt) {
        serial_printf("[ACPI] MADT found, LAPIC phys=0x%x flags=0x%x\n",
                      madt->lapic_addr, madt->flags);
        parse_madt(madt);
    } else {
        serial_puts("[ACPI] MADT not found\n");
        /* Default fallback */
        g_acpi.lapic_phys = 0xFEE00000;
        g_acpi.has_8259   = true;
    }

    serial_printf("[ACPI] init OK — %u CPUs, %u IOAPICs\n",
                  g_acpi.cpu_count, g_acpi.ioapic_count);
    return true;
}

void acpi_shutdown(void)
{
    /*
     * QEMU PIIX4 PM shutdown: write SLP_TYP=5 + SLP_EN to PM1a control.
     * This is the well-known QEMU workaround (port 0x604, value 0x2000).
     * On real hardware the FADT's pm1a_cnt_blk + DSDT SLP_TYP values
     * are needed, which requires AML evaluation (not implemented).
     */
    serial_puts("[ACPI] Shutdown requested\n");
    /* QEMU ACPI shutdown */
    outw(0x604, 0x2000);
    /* Bochs/old QEMU */
    outw(0xB004, 0x2000);
    /* If we're still running, HLT */
    for (;;) __asm__ __volatile__("cli; hlt");
}

void acpi_reboot(void)
{
    serial_puts("[ACPI] Reboot via ACPI reset\n");
    /* ACPI reset register (FADT) — try standard 0xCF9 reset */
    outb(0xCF9, 0x06);
    /* Fallback: PS/2 controller reset */
    outb(0x64, 0xFE);
    for (;;) __asm__ __volatile__("cli; hlt");
}
