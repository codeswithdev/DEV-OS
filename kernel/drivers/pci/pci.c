/*
 * DevOS — PCI bus driver
 *
 * Uses the legacy I/O port mechanism (PCI Configuration Access Mechanism 1):
 *   Port 0xCF8 — CONFIG_ADDRESS: [31]=enable [30:24]=reserved
 *                [23:16]=bus [15:11]=device [10:8]=function [7:2]=reg [1:0]=0
 *   Port 0xCFC — CONFIG_DATA: 32-bit data
 *
 * Performs a full bus 0–255 scan at startup and caches up to PCI_MAX_DEVICES
 * device descriptors.  Most systems have well under 256 PCI devices.
 *
 * Class codes of interest:
 *   0x01/0x01 — IDE controller
 *   0x01/0x06 — AHCI controller
 *   0x01/0x08 — NVMe controller
 *   0x02/0x00 — Ethernet
 *   0x03/0x00 — VGA display
 *   0x04/0x03 — HD Audio
 *   0x06/0x00 — Host bridge
 *   0x0C/0x03 — USB (prog_if: 0x00=UHCI 0x10=OHCI 0x20=EHCI 0x30=xHCI)
 */
#include "pci.h"
#include "../../arch/x86_64/serial.h"
#include "../../mm/vmm.h"
#include "../../lib/string.h"
#include "../../lib/printf.h"

pci_device_t pci_devices[PCI_MAX_DEVICES];
int          pci_device_count = 0;

#define PCI_ADDR_PORT   0xCF8
#define PCI_DATA_PORT   0xCFC

/* ---- I/O helpers ---- */
static inline void outl(uint16_t port, uint32_t val)
{
    __asm__ __volatile__("outl %0,%1" :: "a"(val), "Nd"(port) : "memory");
}
static inline uint32_t inl(uint16_t port)
{
    uint32_t v;
    __asm__ __volatile__("inl %1,%0" : "=a"(v) : "Nd"(port));
    return v;
}
static inline void outb(uint16_t port, uint8_t val)
{
    __asm__ __volatile__("outb %0,%1" :: "a"(val), "Nd"(port) : "memory");
}
static inline void outw(uint16_t port, uint16_t val)
{
    __asm__ __volatile__("outw %0,%1" :: "a"(val), "Nd"(port) : "memory");
}
static inline uint8_t inb_p(uint16_t port)
{
    uint8_t v;
    __asm__ __volatile__("inb %1,%0" : "=a"(v) : "Nd"(port));
    return v;
}
static inline uint16_t inw(uint16_t port)
{
    uint16_t v;
    __asm__ __volatile__("inw %1,%0" : "=a"(v) : "Nd"(port));
    return v;
}

static uint32_t pci_address(uint8_t bus, uint8_t dev, uint8_t func, uint8_t reg)
{
    return (uint32_t)(0x80000000UL
           | ((uint32_t)bus  << 16)
           | ((uint32_t)dev  << 11)
           | ((uint32_t)func <<  8)
           | (reg & 0xFC));
}

/* ---- Config space access ---- */

uint32_t pci_read32(uint8_t bus, uint8_t dev, uint8_t func, uint8_t reg)
{
    outl(PCI_ADDR_PORT, pci_address(bus, dev, func, reg));
    return inl(PCI_DATA_PORT);
}

uint16_t pci_read16(uint8_t bus, uint8_t dev, uint8_t func, uint8_t reg)
{
    outl(PCI_ADDR_PORT, pci_address(bus, dev, func, reg));
    return inw((uint16_t)(PCI_DATA_PORT + (reg & 2)));
}

uint8_t pci_read8(uint8_t bus, uint8_t dev, uint8_t func, uint8_t reg)
{
    outl(PCI_ADDR_PORT, pci_address(bus, dev, func, reg));
    return inb_p((uint16_t)(PCI_DATA_PORT + (reg & 3)));
}

void pci_write32(uint8_t bus, uint8_t dev, uint8_t func, uint8_t reg, uint32_t val)
{
    outl(PCI_ADDR_PORT, pci_address(bus, dev, func, reg));
    outl(PCI_DATA_PORT, val);
}

void pci_write16(uint8_t bus, uint8_t dev, uint8_t func, uint8_t reg, uint16_t val)
{
    outl(PCI_ADDR_PORT, pci_address(bus, dev, func, reg));
    outw((uint16_t)(PCI_DATA_PORT + (reg & 2)), val);
}

/* ---- Command register helpers ---- */
#define PCI_CMD_IO_SPACE    0x0001
#define PCI_CMD_MEM_SPACE   0x0002
#define PCI_CMD_BUS_MASTER  0x0004

void pci_enable_bus_master(pci_device_t *d)
{
    uint16_t cmd = pci_read16(d->bus, d->device, d->function, 0x04);
    cmd |= PCI_CMD_BUS_MASTER;
    pci_write16(d->bus, d->device, d->function, 0x04, cmd);
}

void pci_enable_memory_space(pci_device_t *d)
{
    uint16_t cmd = pci_read16(d->bus, d->device, d->function, 0x04);
    cmd |= PCI_CMD_MEM_SPACE;
    pci_write16(d->bus, d->device, d->function, 0x04, cmd);
}

void pci_enable_io_space(pci_device_t *d)
{
    uint16_t cmd = pci_read16(d->bus, d->device, d->function, 0x04);
    cmd |= PCI_CMD_IO_SPACE;
    pci_write16(d->bus, d->device, d->function, 0x04, cmd);
}

/* ---- BAR helpers ---- */

bool pci_bar_is_mmio(pci_device_t *d, int idx)
{
    return (d->bar[idx] & 1) == 0;
}

bool pci_bar_is_64bit(pci_device_t *d, int idx)
{
    return pci_bar_is_mmio(d, idx) && ((d->bar[idx] >> 1) & 3) == 2;
}

uint64_t pci_bar_address(pci_device_t *d, int idx)
{
    if (!pci_bar_is_mmio(d, idx)) {
        return (uint64_t)(d->bar[idx] & ~0x3U);   /* I/O port */
    }
    uint64_t addr = d->bar[idx] & ~0xFU;
    if (pci_bar_is_64bit(d, idx) && idx < 5) {
        addr |= ((uint64_t)d->bar[idx + 1]) << 32;
    }
    return addr;
}

void *pci_map_bar(pci_device_t *d, int idx)
{
    if (!pci_bar_is_mmio(d, idx)) return NULL;
    uint64_t phys  = pci_bar_address(d, idx);
    uint64_t virt  = KERNEL_VIRT_BASE + phys;
    /* Map 4 pages (16 KB) — sufficient for most control registers */
    vmm_map_range(kernel_pml4, virt, phys, 4 * PAGE_SIZE, VMM_MMIO);
    return (void *)(uintptr_t)virt;
}

/* ---- Device scanning ---- */

static void scan_function(uint8_t bus, uint8_t dev, uint8_t func)
{
    uint32_t id = pci_read32(bus, dev, func, 0x00);
    if ((id & 0xFFFF) == 0xFFFF) return;   /* no device */

    if (pci_device_count >= PCI_MAX_DEVICES) return;

    pci_device_t *d = &pci_devices[pci_device_count++];
    d->bus       = bus;
    d->device    = dev;
    d->function  = func;
    d->vendor_id = (uint16_t)(id & 0xFFFF);
    d->device_id = (uint16_t)(id >> 16);

    uint32_t class_rev = pci_read32(bus, dev, func, 0x08);
    d->revision    = (uint8_t)(class_rev & 0xFF);
    d->prog_if     = (uint8_t)((class_rev >> 8)  & 0xFF);
    d->subclass    = (uint8_t)((class_rev >> 16) & 0xFF);
    d->class_code  = (uint8_t)((class_rev >> 24) & 0xFF);

    uint32_t hdr = pci_read32(bus, dev, func, 0x0C);
    d->header_type = (uint8_t)((hdr >> 16) & 0xFF);

    /* Read BARs (type 0 headers have 6 BARs) */
    if ((d->header_type & 0x7F) == 0) {
        for (int i = 0; i < 6; i++) {
            d->bar[i] = pci_read32(bus, dev, func, (uint8_t)(0x10 + i * 4));
        }
    }

    uint32_t irq = pci_read32(bus, dev, func, 0x3C);
    d->irq_line = (uint8_t)(irq & 0xFF);
    d->irq_pin  = (uint8_t)((irq >> 8) & 0xFF);
}

static void scan_device(uint8_t bus, uint8_t dev)
{
    uint32_t id = pci_read32(bus, dev, 0, 0x00);
    if ((id & 0xFFFF) == 0xFFFF) return;

    scan_function(bus, dev, 0);

    /* Check for multifunction device */
    uint32_t hdr = pci_read32(bus, dev, 0, 0x0C);
    if ((hdr >> 16) & 0x80) {
        for (uint8_t f = 1; f < 8; f++) {
            uint32_t fid = pci_read32(bus, dev, f, 0x00);
            if ((fid & 0xFFFF) != 0xFFFF) scan_function(bus, dev, f);
        }
    }
}

void pci_scan(void)
{
    pci_device_count = 0;
    for (uint16_t bus = 0; bus < 256; bus++) {
        for (uint8_t dev = 0; dev < 32; dev++) {
            scan_device((uint8_t)bus, dev);
        }
    }
    serial_printf("[PCI] scan complete: %d devices found\n", pci_device_count);
}

void pci_init(void)
{
    pci_scan();
}

/* ---- Lookup ---- */

pci_device_t *pci_find_device(uint16_t vendor, uint16_t device_id)
{
    for (int i = 0; i < pci_device_count; i++) {
        if (pci_devices[i].vendor_id == vendor &&
            pci_devices[i].device_id == device_id)
            return &pci_devices[i];
    }
    return NULL;
}

pci_device_t *pci_find_class(uint8_t class_code, uint8_t subclass)
{
    for (int i = 0; i < pci_device_count; i++) {
        if (pci_devices[i].class_code == class_code &&
            pci_devices[i].subclass   == subclass)
            return &pci_devices[i];
    }
    return NULL;
}

pci_device_t *pci_find_class_progif(uint8_t class_code, uint8_t subclass, uint8_t prog_if)
{
    for (int i = 0; i < pci_device_count; i++) {
        if (pci_devices[i].class_code == class_code &&
            pci_devices[i].subclass   == subclass   &&
            pci_devices[i].prog_if    == prog_if)
            return &pci_devices[i];
    }
    return NULL;
}

/* ---- Shell: lspci ---- */

static const char *pci_class_name(uint8_t cls, uint8_t sub)
{
    if (cls == 0x00) return "Unclassified";
    if (cls == 0x01) {
        if (sub == 0x01) return "IDE Controller";
        if (sub == 0x06) return "SATA/AHCI Controller";
        if (sub == 0x08) return "NVMe Controller";
        return "Storage Controller";
    }
    if (cls == 0x02) {
        if (sub == 0x00) return "Ethernet Controller";
        return "Network Controller";
    }
    if (cls == 0x03) return "VGA Controller";
    if (cls == 0x04) {
        if (sub == 0x03) return "HD Audio Controller";
        return "Multimedia Controller";
    }
    if (cls == 0x06) {
        if (sub == 0x00) return "Host Bridge";
        if (sub == 0x01) return "ISA Bridge";
        if (sub == 0x04) return "PCI-to-PCI Bridge";
        return "Bridge";
    }
    if (cls == 0x0C) {
        if (sub == 0x03) return "USB Controller";
        return "Serial Bus Controller";
    }
    return "Unknown";
}

void pci_print_devices(void)
{
    if (pci_device_count == 0) {
        serial_puts("No PCI devices found\n");
        return;
    }
    char buf[96];
    for (int i = 0; i < pci_device_count; i++) {
        pci_device_t *d = &pci_devices[i];
        snprintf(buf, sizeof(buf),
                 "%02x:%02x.%u  %04x:%04x  [%02x:%02x:%02x]  %s\n",
                 d->bus, d->device, d->function,
                 d->vendor_id, d->device_id,
                 d->class_code, d->subclass, d->prog_if,
                 pci_class_name(d->class_code, d->subclass));
        /* Output to VGA + serial via extern shell helpers */
        extern void vga_puts(const char *);
        vga_puts(buf);
        serial_puts(buf);
    }
}
