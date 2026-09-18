/* DevOS — Basic graphics primitives (gfx.h) */
#pragma once
#include "../../include/types.h"

void gfx_draw_rect(uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint32_t color);
void gfx_draw_line(int x0, int y0, int x1, int y1, uint32_t color);
void gfx_draw_char(uint32_t x, uint32_t y, char c, uint32_t fg, uint32_t bg);
void gfx_draw_string(uint32_t x, uint32_t y, const char *s, uint32_t fg, uint32_t bg);
