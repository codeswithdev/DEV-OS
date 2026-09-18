/*
 * DevOS — Console abstraction layer (console.c)
 *
 * Routes console_* calls to the VGA text driver.
 * When a linear framebuffer becomes available (via bootloader or VESA),
 * set active_backend = CONSOLE_FB and the same API will route to fb_putc.
 */
#include "console.h"
#include "vga.h"
#include "../arch/x86_64/serial.h"

typedef enum {
    CONSOLE_VGA = 0,
    CONSOLE_FB  = 1,
} console_backend_t;

static console_backend_t active_backend = CONSOLE_VGA;
static bool console_ready = false;

void console_init(void)
{
    /* VGA already initialised by kmain; we just record we're ready */
    active_backend = CONSOLE_VGA;
    console_ready  = true;
    serial_puts("[CONSOLE] init OK (backend=VGA)\n");
}

void console_putc(char c)
{
    if (!console_ready) return;
    switch (active_backend) {
    case CONSOLE_VGA:
        vga_putchar(c);
        break;
    case CONSOLE_FB:
        /* TODO: route to framebuffer text renderer */
        vga_putchar(c);   /* fallback until FB text is ready */
        break;
    }
    serial_putchar((uint8_t)c);
}

void console_write(const char *s)
{
    if (!s) return;
    while (*s) console_putc(*s++);
}

void console_clear(void)
{
    if (!console_ready) return;
    vga_clear();
}

void console_set_color(uint8_t fg, uint8_t bg)
{
    vga_set_color((vga_color_t)fg, (vga_color_t)bg);
}

bool framebuffer_available(void)
{
    /*
     * The current BIOS bootloader does not set up a linear framebuffer.
     * This returns false until stage2 is extended with VESA VBE support
     * or a multiboot-compatible bootloader is used.
     */
    return false;
}
