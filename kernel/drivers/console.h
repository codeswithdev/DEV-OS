/*
 * DevOS — Console abstraction layer
 * Provides a unified API over VGA text mode (and future framebuffer).
 * All kernel output should go through console_* rather than vga_* directly,
 * so we can swap in a framebuffer backend later without touching callers.
 */
#pragma once
#include "../include/types.h"

void console_init(void);
void console_putc(char c);
void console_write(const char *s);
void console_clear(void);
void console_set_color(uint8_t fg, uint8_t bg);

/* Framebuffer detection/init — returns true if a linear FB is available */
bool framebuffer_available(void);
