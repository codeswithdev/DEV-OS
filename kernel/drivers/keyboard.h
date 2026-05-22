#pragma once
#include "../include/types.h"

void keyboard_init(void);
int  keyboard_poll(void);       /* non-blocking: returns -1 if empty */
int  keyboard_read_char(void);  /* blocking: waits for a character */
