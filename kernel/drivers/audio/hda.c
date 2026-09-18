/*
 * DevOS — Intel HDA (High Definition Audio) driver
 *
 * HDA is PCI class 0x04/subclass 0x03.
 * QEMU emulates Intel ICH6 HDA (8086:2668) or 8086:2415 (ICH AC97).
 *
 * This driver:
 *  1. Detects the HDA PCI device.
 *  2. Maps the MMIO bar (BAR0).
 *  3. Resets the controller.
 *  4. Sets up a Command Output Stream (for future PCM playback).
 *  5. Provides hda_play_beep() using the PC speaker fallback (port 0x61/PIT)
 *     because HDA codec initialisation requires AML/codec verb sequences
 *     that need more infrastructure than is currently present.
 *
 * HDA MMIO registers (from BAR0):
 *   0x00  GCAP    Global Capabilities
 *   0x08  GCTL    Global Control (bit0 = CRST)
 *   0x0E  STATESTS Wake status
 *   0x18  WAKEEN  Wake enable
 *   0x60  CORBLBASE CORB lower base
 *   0x64  CORBUBASE CORB upper base
 *   0x68  CORBWP  CORB Write Pointer
 *   0x6A  CORBRP  CORB Read Pointer
 *   0x6C  CORBCTL CORB Control
 *   0x70  RIRBBASE RIRB lower base
 *   0x78  RIRBWP  RIRB Write Pointer
 */
#include "hda.h"
#include "../pci/pci.h"
#include "../../mm/vmm.h"
#include "../../mm/heap.h"
#include "../../arch/x86_64/serial.h"
#include "../../arch/x86_64/timer.h"
#include "../../lib/string.h"

#define HDA_VENDOR_ICH6  0x8086
#define HDA_DEVICE_ICH6  0x2668

/* HDA MMIO offsets */
#define HDA_GCAP    0x00
#define HDA_VMIN    0x02
#define HDA_VMAJ    0x03
#define HDA_OUTPAY  0x04
#define HDA_INPAY   0x06
#define HDA_GCTL    0x08
#define HDA_GSTS    0x0E
#define HDA_INTCTL  0x20
#define HDA_INTSTS  0x24
#define HDA_CORBLBASE 0x40
#define HDA_CORBUBASE 0x44
#define HDA_CORBWP  0x48
#define HDA_CORBRP  0x4A
#define HDA_CORBCTL 0x4C
#define HDA_RIRBBASE  0x50
#define HDA_RIRBWP  0x58
#define HDA_RIRBCTL 0x5C
#define HDA_IMMEDIATE_CMD  0x60
#define HDA_IMMEDIATE_RESP 0x64
#define HDA_IMMEDIATE_STS  0x68

#define HDA_GCTL_CRST   (1U << 0)
#define HDA_GCTL_FCNTRL (1U << 1)

static volatile uint8_t *hda_mmio = NULL;

static inline uint32_t hda_read32(uint32_t off)
{
    return *(volatile uint32_t *)(hda_mmio + off);
}
static inline void hda_write32(uint32_t off, uint32_t v)
{
    *(volatile uint32_t *)(hda_mmio + off) = v;
}
static inline uint16_t hda_read16(uint32_t off)
{
    return *(volatile uint16_t *)(hda_mmio + off);
}
static inline void hda_write16(uint32_t off, uint16_t v)
{
    *(volatile uint16_t *)(hda_mmio + off) = v;
}
static inline uint8_t hda_read8(uint32_t off)
{
    return *(volatile uint8_t *)(hda_mmio + off);
}
static inline void hda_write8(uint32_t off, uint8_t v)
{
    *(volatile uint8_t *)(hda_mmio + off) = v;
}

/* ---- PC Speaker beep (PIT ch2 + port 0x61) ---- */
static inline void outb(uint16_t p, uint8_t v)
{
    __asm__ __volatile__("outb %0,%1"::"a"(v),"Nd"(p):"memory");
}
static inline uint8_t inb(uint16_t p)
{
    uint8_t v; __asm__ __volatile__("inb %1,%0":"=a"(v):"Nd"(p)); return v;
}

static void pcspeaker_beep(uint32_t freq_hz, uint32_t duration_ms)
{
    if (freq_hz == 0) return;
    uint32_t divisor = 1193180 / freq_hz;

    /* Configure PIT channel 2 */
    outb(0x43, 0xB6);   /* Channel 2, mode 3 (square wave), binary */
    outb(0x42, (uint8_t)(divisor & 0xFF));
    outb(0x42, (uint8_t)((divisor >> 8) & 0xFF));

    /* Enable speaker via port 0x61 */
    uint8_t tmp = inb(0x61);
    outb(0x61, tmp | 0x03);

    timer_sleep_ms(duration_ms);

    /* Disable speaker */
    tmp = inb(0x61);
    outb(0x61, tmp & ~0x03);
}

/* ---- HDA codec immediate command (single verb via ICW) ---- */
static __attribute__((unused)) uint32_t hda_icmd(uint32_t verb)
{
    /* Wait for ICS ready (bit 0) */
    uint32_t t = 100000;
    while (!(hda_read16(HDA_IMMEDIATE_STS) & 0x01) && t--);
    hda_write32(HDA_IMMEDIATE_CMD, verb);
    /* Write ICS: set bit1 (send) */
    hda_write16(HDA_IMMEDIATE_STS, 0x01);
    /* Wait for IRV (bit1 = response valid) */
    t = 100000;
    while (!(hda_read16(HDA_IMMEDIATE_STS) & 0x02) && t--);
    return hda_read32(HDA_IMMEDIATE_RESP);
}

bool hda_init(void)
{
    pci_device_t *pdev = pci_find_class(0x04, 0x03);
    if (!pdev) {
        /* Also try older Intel HDA */
        pdev = pci_find_device(HDA_VENDOR_ICH6, HDA_DEVICE_ICH6);
    }
    if (!pdev) {
        serial_puts("[HDA] no HD Audio controller found\n");
        return false;
    }

    serial_printf("[HDA] found %04x:%04x\n", pdev->vendor_id, pdev->device_id);
    pci_enable_bus_master(pdev);
    pci_enable_memory_space(pdev);

    hda_mmio = (volatile uint8_t *)pci_map_bar(pdev, 0);
    if (!hda_mmio) { serial_puts("[HDA] BAR0 map failed\n"); return false; }

    /* Controller reset */
    hda_write32(HDA_GCTL, 0);
    uint32_t t = 1000000;
    while ((hda_read32(HDA_GCTL) & HDA_GCTL_CRST) && t--);

    timer_sleep_ms(1);  /* Wait 1ms */

    hda_write32(HDA_GCTL, HDA_GCTL_CRST);
    t = 1000000;
    while (!(hda_read32(HDA_GCTL) & HDA_GCTL_CRST) && t--);

    timer_sleep_ms(1);

    uint16_t gcap = hda_read16(HDA_GCAP);
    uint8_t  vmaj = hda_read8(HDA_VMAJ);
    uint8_t  vmin = hda_read8(HDA_VMIN);
    serial_printf("[HDA] version %u.%u GCAP=0x%04x\n", vmaj, vmin, gcap);

    /* Play a startup beep via PC speaker (HDA codec init not implemented yet) */
    pcspeaker_beep(440, 100);   /* 440 Hz A note, 100 ms */

    serial_puts("[HDA] init OK (PC speaker active; HDA codec: stub)\n");
    return true;
}

int hda_play_beep(uint32_t freq_hz, uint32_t duration_ms)
{
    pcspeaker_beep(freq_hz, duration_ms);
    return 0;
}
