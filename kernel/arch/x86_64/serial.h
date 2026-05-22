#pragma once
#include "../../include/types.h"

void serial_init(void);
void serial_putchar(char c);
void serial_puts(const char *s);
void serial_printf(const char *fmt, ...) __attribute__((format(__printf__, 1, 2)));
