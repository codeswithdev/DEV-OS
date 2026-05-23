#pragma once
#include "../include/types.h"

typedef enum {
    VGA_COLOR_BLACK = 0, VGA_COLOR_BLUE, VGA_COLOR_GREEN, VGA_COLOR_CYAN,
    VGA_COLOR_RED, VGA_COLOR_MAGENTA, VGA_COLOR_BROWN, VGA_COLOR_LIGHT_GREY,
    VGA_COLOR_DARK_GREY, VGA_COLOR_LIGHT_BLUE, VGA_COLOR_LIGHT_GREEN,
    VGA_COLOR_LIGHT_CYAN, VGA_COLOR_LIGHT_RED, VGA_COLOR_LIGHT_MAGENTA,
    VGA_COLOR_LIGHT_BROWN, VGA_COLOR_WHITE,
} vga_color_t;

void vga_init(void);
void vga_putchar(char c);
void vga_puts(const char *s);
void vga_set_color(vga_color_t fg, vga_color_t bg);
void vga_clear(void);
