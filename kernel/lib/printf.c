#include "printf.h"
#include "string.h"

static void put_uint(char *buf, size_t *pos, size_t sz,
                     uint64_t val, int base, int upper, int width, int zero_pad)
{
    static const char digits_lo[] = "0123456789abcdef";
    static const char digits_hi[] = "0123456789ABCDEF";
    const char *digits = upper ? digits_hi : digits_lo;
    char tmp[20];
    int  len = 0;

    if (val == 0) { tmp[len++] = '0'; }
    else {
        uint64_t v = val;
        while (v) { tmp[len++] = digits[v % base]; v /= base; }
    }

    /* Pad */
    char pad = zero_pad ? '0' : ' ';
    while (width > len) {
        if (*pos < sz - 1) buf[(*pos)++] = pad;
        width--;
    }

    /* Reverse */
    for (int i = len - 1; i >= 0; i--) {
        if (*pos < sz - 1) buf[(*pos)++] = tmp[i];
    }
}

int kvsnprintf(char *buf, size_t sz, const char *fmt, __builtin_va_list ap)
{
    if (!buf || sz == 0) return 0;
    size_t pos = 0;

#define OUT(c) do { if (pos < sz - 1) buf[pos++] = (c); } while(0)

    while (*fmt) {
        if (*fmt != '%') { OUT(*fmt++); continue; }
        fmt++;

        int zero_pad = 0, width = 0, is_long = 0;

        if (*fmt == '0') { zero_pad = 1; fmt++; }
        while (*fmt >= '0' && *fmt <= '9') { width = width * 10 + (*fmt++ - '0'); }
        if (*fmt == 'l') { is_long = 1; fmt++; if (*fmt == 'l') fmt++; }
        if (*fmt == 'z') { is_long = 1; fmt++; }

        switch (*fmt) {
        case 'd': case 'i': {
            int64_t v = is_long ? (int64_t)__builtin_va_arg(ap, long long)
                                : (int64_t)__builtin_va_arg(ap, int);
            if (v < 0) { OUT('-'); v = -v; }
            put_uint(buf, &pos, sz, (uint64_t)v, 10, 0, width, zero_pad);
            break;
        }
        case 'u': {
            uint64_t v = is_long ? __builtin_va_arg(ap, unsigned long long)
                                 : __builtin_va_arg(ap, unsigned int);
            put_uint(buf, &pos, sz, v, 10, 0, width, zero_pad);
            break;
        }
        case 'x': {
            uint64_t v = is_long ? __builtin_va_arg(ap, unsigned long long)
                                 : __builtin_va_arg(ap, unsigned int);
            put_uint(buf, &pos, sz, v, 16, 0, width, zero_pad);
            break;
        }
        case 'X': {
            uint64_t v = is_long ? __builtin_va_arg(ap, unsigned long long)
                                 : __builtin_va_arg(ap, unsigned int);
            put_uint(buf, &pos, sz, v, 16, 1, width, zero_pad);
            break;
        }
        case 'p': {
            uint64_t v = (uint64_t)(uintptr_t)__builtin_va_arg(ap, void *);
            OUT('0'); OUT('x');
            put_uint(buf, &pos, sz, v, 16, 0, 16, 1);
            break;
        }
        case 'c': {
            char c = (char)__builtin_va_arg(ap, int);
            OUT(c);
            break;
        }
        case 's': {
            const char *s = __builtin_va_arg(ap, const char *);
            if (!s) s = "(null)";
            while (*s) OUT(*s++);
            break;
        }
        case '%':
            OUT('%');
            break;
        default:
            OUT('%'); OUT(*fmt);
            break;
        }
        fmt++;
    }

#undef OUT
    buf[pos] = '\0';
    return (int)pos;
}

int ksnprintf(char *buf, size_t sz, const char *fmt, ...)
{
    __builtin_va_list ap;
    __builtin_va_start(ap, fmt);
    int r = kvsnprintf(buf, sz, fmt, ap);
    __builtin_va_end(ap);
    return r;
}
