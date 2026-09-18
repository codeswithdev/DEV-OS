/*
 * DevOS — Linear Framebuffer driver
 *
 * Supports two discovery paths:
 *
 * 1. Multiboot/VESA: If the bootloader passed a framebuffer info structure
 *    (via a tag or fixed boot_info field), use those dimensions/address.
 *    Current DEV-OS bootloader does NOT do this (BIOS text mode only).
 *
 * 2. PCI VGA BAR (MMIO): Scan for a VGA device (class 0x03/0x00) and attempt
 *    to use BAR0/BAR1 as a linear framebuffer.  This works with QEMU's
 *    stdvga (bochs-display) in certain modes but NOT with the standard
 *    VGA text-mode controller.
 *
 * Since the current bootloader is text-only, fb_init() will report no FB
 * unless the bootloader is upgraded to set a VESA/UEFI framebuffer.
 * The stub is here so drivers/console.c can call fb_init() safely.
 */
#include "framebuffer.h"
#include "../pci/pci.h"
#include "../../mm/vmm.h"
#include "../../arch/x86_64/serial.h"
#include "../../lib/string.h"

framebuffer_t g_fb;

/*
 * Try to get framebuffer info from the boot_info structure.
 * The current DEV-OS boot_info_t does not carry FB data —
 * this is a placeholder for when the bootloader is upgraded.
 */
static bool fb_from_boot_info(void)
{
    /* TODO: check if boot_info has framebuffer fields */
    return false;
}

/*
 * Attempt to map a PCI VGA BAR as a framebuffer.
 * QEMU bochs-display: vendor=0x1234, device=0x1111, BAR0 = LFB.
 * This only works if QEMU was started with -vga std (which provides bochs).
 */
static bool fb_from_pci(void)
{
    /* Bochs/QEMU standard VGA */
    pci_device_t *pdev = pci_find_device(0x1234, 0x1111);
    if (!pdev) {
        pdev = pci_find_class(0x03, 0x00);  /* Generic VGA */
    }
    if (!pdev) return false;

    uint64_t bar0 = pci_bar_address(pdev, 0);
    if (!bar0 || !pci_bar_is_mmio(pdev, 0)) return false;

    /* Default to 1024×768×32 if no VBE info is available */
    g_fb.phys_base    = bar0;
    g_fb.width        = 1024;
    g_fb.height       = 768;
    g_fb.bpp          = 32;
    g_fb.pitch        = 1024 * 4;
    g_fb.red_offset   = 16;
    g_fb.green_offset = 8;
    g_fb.blue_offset  = 0;

    /* Map 3 MB of VRAM */
    uint64_t virt = KERNEL_VIRT_BASE + bar0;
    vmm_map_range(kernel_pml4, virt, bar0, 3 * 1024 * 1024, VMM_MMIO);
    g_fb.virt_base = (void *)(uintptr_t)virt;
    g_fb.valid     = true;

    serial_printf("[FB] mapped bochs VGA %ux%u bpp=%u phys=0x%llx\n",
                  g_fb.width, g_fb.height, g_fb.bpp, bar0);
    return true;
}

bool fb_init(void)
{
    memset(&g_fb, 0, sizeof(g_fb));

    if (fb_from_boot_info()) return true;
    if (fb_from_pci())       return true;

    serial_puts("[FB] no framebuffer available (text mode only)\n");
    return false;
}

uint32_t fb_make_color(uint8_t r, uint8_t g, uint8_t b)
{
    return ((uint32_t)r << g_fb.red_offset)   |
           ((uint32_t)g << g_fb.green_offset) |
           ((uint32_t)b << g_fb.blue_offset);
}

void fb_put_pixel(uint32_t x, uint32_t y, uint32_t color)
{
    if (!g_fb.valid || x >= g_fb.width || y >= g_fb.height) return;
    uint32_t *row = (uint32_t *)((uint8_t *)g_fb.virt_base + y * g_fb.pitch);
    row[x] = color;
}

void fb_clear(uint32_t color)
{
    if (!g_fb.valid) return;
    uint32_t *fb = (uint32_t *)g_fb.virt_base;
    uint32_t pixels = g_fb.height * (g_fb.pitch / (g_fb.bpp / 8));
    for (uint32_t i = 0; i < pixels; i++) fb[i] = color;
}
