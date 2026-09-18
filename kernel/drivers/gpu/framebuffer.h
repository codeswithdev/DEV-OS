/* DevOS — Linear framebuffer abstraction (framebuffer.h) */
#pragma once
#include "../../include/types.h"

typedef struct {
    uint64_t phys_base;
    void    *virt_base;
    uint32_t width;
    uint32_t height;
    uint32_t pitch;         /* bytes per row */
    uint8_t  bpp;           /* bits per pixel */
    uint8_t  red_offset;
    uint8_t  green_offset;
    uint8_t  blue_offset;
    bool     valid;
} framebuffer_t;

extern framebuffer_t g_fb;

bool fb_init(void);         /* detect and map framebuffer */
void fb_clear(uint32_t color);
void fb_put_pixel(uint32_t x, uint32_t y, uint32_t argb);
uint32_t fb_make_color(uint8_t r, uint8_t g, uint8_t b);
