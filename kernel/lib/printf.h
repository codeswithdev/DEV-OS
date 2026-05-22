#pragma once
#include "../include/types.h"

/* Minimal kernel printf writing to a caller-supplied output function */
typedef void (*printf_putchar_fn)(char);

int kvsnprintf(char *buf, size_t sz, const char *fmt, __builtin_va_list ap);
int ksnprintf(char *buf, size_t sz, const char *fmt, ...);

#define snprintf ksnprintf
