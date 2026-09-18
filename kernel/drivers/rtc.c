/*
 * DevOS — CMOS RTC driver
 *
 * CMOS access:
 *   Write register index to port 0x70 (with NMI disable in bit 7)
 *   Read/write data from port 0x71
 *
 * CMOS RTC registers:
 *   0x00  seconds   0x02  minutes   0x04  hours
 *   0x06  weekday   0x07  day       0x08  month   0x09  year
 *   0x0B  status B  (bit2 = binary mode, bit1 = 24h mode)
 *   0x0A  status A  (bit7 = UIP — update in progress)
 *   0x32  century   (may not exist on all systems)
 */
#include "rtc.h"
#include "../arch/x86_64/serial.h"
#include "../lib/printf.h"
#include "../drivers/vga.h"

#define CMOS_ADDR   0x70
#define CMOS_DATA   0x71
#define RTC_UIP     0x80    /* update-in-progress flag in register A */

static inline void outb(uint16_t port, uint8_t val)
{
    __asm__ __volatile__("outb %0,%1" :: "a"(val), "Nd"(port) : "memory");
}
static inline uint8_t inb(uint16_t port)
{
    uint8_t v;
    __asm__ __volatile__("inb %1,%0" : "=a"(v) : "Nd"(port));
    return v;
}

/* Read one CMOS register.  Disables NMI while accessing. */
static uint8_t cmos_read(uint8_t reg)
{
    outb(CMOS_ADDR, (uint8_t)(0x80 | reg));  /* NMI disable + register */
    return inb(CMOS_DATA);
}

/* Wait until the RTC is not updating */
static void rtc_wait_ready(void)
{
    /* Spin on UIP flag in Status Register A (reg 0x0A) */
    uint32_t timeout = 1000000;
    while ((cmos_read(0x0A) & RTC_UIP) && timeout--);
}

/* BCD to binary conversion */
static uint8_t bcd2bin(uint8_t bcd)
{
    return (uint8_t)(((bcd >> 4) & 0x0F) * 10 + (bcd & 0x0F));
}

void rtc_init(void)
{
    rtc_time_t t;
    rtc_get_time(&t);
    serial_printf("[RTC] init OK — %04u-%02u-%02u %02u:%02u:%02u\n",
                  t.year, t.month, t.day,
                  t.hour, t.minute, t.second);
}

void rtc_get_time(rtc_time_t *t)
{
    if (!t) return;

    /* Read until two consecutive identical reads (guards against update mid-read) */
    rtc_time_t a, b;
    uint8_t status_b;

    rtc_wait_ready();
    a.second  = cmos_read(0x00);
    a.minute  = cmos_read(0x02);
    a.hour    = cmos_read(0x04);
    a.weekday = cmos_read(0x06);
    a.day     = cmos_read(0x07);
    a.month   = cmos_read(0x08);
    a.year    = cmos_read(0x09);
    status_b  = cmos_read(0x0B);

    /* Verify consistent read */
    rtc_wait_ready();
    b.second  = cmos_read(0x00);
    b.minute  = cmos_read(0x02);
    b.hour    = cmos_read(0x04);
    b.weekday = cmos_read(0x06);
    b.day     = cmos_read(0x07);
    b.month   = cmos_read(0x08);
    b.year    = cmos_read(0x09);

    /* Pick second read (most recent) */
    *t = b;
    (void)a;

    /* Convert BCD to binary if necessary (bit2 of Status B = 0 means BCD) */
    if (!(status_b & 0x04)) {
        t->second  = bcd2bin(t->second);
        t->minute  = bcd2bin(t->minute);
        t->hour    = bcd2bin(t->hour & 0x7F);  /* mask AM/PM bit */
        t->weekday = bcd2bin(t->weekday);
        t->day     = bcd2bin(t->day);
        t->month   = bcd2bin(t->month);
        t->year    = bcd2bin((uint8_t)t->year);
    }

    /* 12-hour to 24-hour conversion */
    if (!(status_b & 0x02)) {
        uint8_t raw_hour = cmos_read(0x04);
        bool pm = (raw_hour & 0x80) != 0;
        if (pm && t->hour < 12) t->hour += 12;
        if (!pm && t->hour == 12) t->hour = 0;
    }

    /* Century: try register 0x32 (may return 0 if not supported) */
    uint8_t century = cmos_read(0x32);
    if (century >= 19 && century <= 21) {
        if (!(status_b & 0x04)) century = bcd2bin(century);
        t->year = (uint16_t)(century * 100 + t->year);
    } else {
        /* Assume 21st century */
        t->year = (uint16_t)(2000 + t->year);
    }
}

void rtc_print_time(void)
{
    rtc_time_t t;
    rtc_get_time(&t);

    char buf[64];
    snprintf(buf, sizeof(buf),
             "Date/Time: %04u-%02u-%02u  %02u:%02u:%02u\n",
             t.year, t.month, t.day,
             t.hour, t.minute, t.second);
    vga_puts(buf);
    serial_puts(buf);
}
