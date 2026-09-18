/*
 * DevOS — PS/2 Mouse driver (mouse.h)
 * IRQ12 / PS/2 auxiliary device
 */
#pragma once
#include "../include/types.h"

typedef struct {
    int8_t  dx;         /* relative X movement (positive = right) */
    int8_t  dy;         /* relative Y movement (positive = up) */
    uint8_t buttons;    /* bit0=left, bit1=right, bit2=middle */
    bool    overflow_x;
    bool    overflow_y;
    bool    valid;
} mouse_event_t;

void          mouse_init(void);
int           mouse_get_event(mouse_event_t *ev);   /* returns 1 on success, 0 = no event */
int           mouse_read(mouse_event_t *ev);        /* blocking */
