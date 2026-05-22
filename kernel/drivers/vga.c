#include "vga.h"
#include "../lib/string.h"

#define VGA_BASE    ((volatile uint16_t *)0xFFFFFFFF800B8000ULL)
#define VGA_COLS    80
#define VGA_ROWS    25

static uint8_t  vga_color;
static uint16_t vga_row, vga_col;

static inline uint16_t make_entry(char c, uint8_t color) {
    return (uint16_t)((uint8_t)c | ((uint16_t)color << 8));
}

void vga_init(void)
{
    vga_color = (uint8_t)(VGA_COLOR_LIGHT_GREY | (VGA_COLOR_BLACK << 4));
    vga_row = vga_col = 0;
    volatile uint16_t *buf = VGA_BASE;
    for (int i = 0; i < VGA_COLS * VGA_ROWS; i++)
        buf[i] = make_entry(' ', vga_color);
}

void vga_set_color(vga_color_t fg, vga_color_t bg)
{
    vga_color = (uint8_t)((uint8_t)fg | ((uint8_t)bg << 4));
}

static void vga_scroll(void)
{
    volatile uint16_t *buf = VGA_BASE;
    for (int r = 0; r < VGA_ROWS - 1; r++)
        for (int c = 0; c < VGA_COLS; c++)
            buf[r * VGA_COLS + c] = buf[(r+1) * VGA_COLS + c];
    for (int c = 0; c < VGA_COLS; c++)
        buf[(VGA_ROWS-1) * VGA_COLS + c] = make_entry(' ', vga_color);
    vga_row = VGA_ROWS - 1;
}

void vga_putchar(char c)
{
    volatile uint16_t *buf = VGA_BASE;
    if (c == '\n') {
        vga_col = 0;
        if (++vga_row >= VGA_ROWS) vga_scroll();
        return;
    }
    if (c == '\r') { vga_col = 0; return; }
    buf[vga_row * VGA_COLS + vga_col] = make_entry(c, vga_color);
    if (++vga_col >= VGA_COLS) { vga_col = 0; if (++vga_row >= VGA_ROWS) vga_scroll(); }
}

void vga_puts(const char *s)
{
    while (*s) vga_putchar(*s++);
}

void vga_clear(void)
{
    volatile uint16_t *buf = VGA_BASE;
    for (int i = 0; i < VGA_COLS * VGA_ROWS; i++)
        buf[i] = make_entry(' ', vga_color);
    vga_row = vga_col = 0;
}
