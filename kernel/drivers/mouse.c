/*
 * DevOS — PS/2 Mouse driver
 *
 * The PS/2 controller uses:
 *   0x60  data port
 *   0x64  status/command port
 *
 * Mouse is the "auxiliary" PS/2 device (IRQ12).
 * The driver uses 3-byte absolute packet mode (Intellimouse scroll wheel
 * is NOT enabled — keeps packets at exactly 3 bytes for simplicity).
 *
 * Packet format (byte 0):
 *   bit7 = Y overflow
 *   bit6 = X overflow
 *   bit5 = Y sign (1 = negative)
 *   bit4 = X sign (1 = negative)
 *   bit3 = always 1 (sync bit — used to re-synchronise)
 *   bit2 = middle button
 *   bit1 = right button
 *   bit0 = left button
 * Byte 1: X movement
 * Byte 2: Y movement
 */
#include "mouse.h"
#include "../arch/x86_64/irq.h"
#include "../arch/x86_64/serial.h"
#include "../sched/sched.h"
#include "../include/spinlock.h"

#define PS2_DATA    0x60
#define PS2_STATUS  0x64
#define PS2_CMD     0x64

#define PS2_STATUS_OBF  (1 << 0)   /* output buffer full  — data ready to read */
#define PS2_STATUS_IBF  (1 << 1)   /* input  buffer full  — busy, don't write */

#define MOUSE_BUF_SIZE  64

static volatile mouse_event_t mouse_buf[MOUSE_BUF_SIZE];
static volatile uint32_t      mouse_head = 0;
static volatile uint32_t      mouse_tail = 0;

static uint8_t  packet[3];
static uint8_t  packet_idx = 0;

/* Tasks blocked waiting for a mouse event */
#define MOUSE_WAIT_MAX  4
static task_t  *mouse_waiters[MOUSE_WAIT_MAX];
static int      mouse_waiter_count = 0;

/* ---------- I/O helpers ---------- */

static inline uint8_t inb(uint16_t port)
{
    uint8_t v;
    __asm__ __volatile__("inb %1,%0" : "=a"(v) : "Nd"(port));
    return v;
}
static inline void outb(uint16_t port, uint8_t val)
{
    __asm__ __volatile__("outb %0,%1" :: "a"(val), "Nd"(port) : "memory");
}

/* Wait until the PS/2 input buffer is ready to accept a byte */
static void ps2_wait_write(void)
{
    uint32_t timeout = 100000;
    while ((inb(PS2_STATUS) & PS2_STATUS_IBF) && timeout--);
}

/* Wait until the PS/2 output buffer has data */
static void ps2_wait_read(void)
{
    uint32_t timeout = 100000;
    while (!(inb(PS2_STATUS) & PS2_STATUS_OBF) && timeout--);
}

/* Send a command to the PS/2 controller (port 0x64) */
static void ps2_cmd(uint8_t cmd)
{
    ps2_wait_write();
    outb(PS2_CMD, cmd);
}

/* Send a byte to the PS/2 data port (0x60) */
static void ps2_write(uint8_t val)
{
    ps2_wait_write();
    outb(PS2_DATA, val);
}

/* Send a byte to the mouse (routes through auxiliary device) */
static void mouse_write(uint8_t val)
{
    ps2_cmd(0xD4);      /* "write to auxiliary device" */
    ps2_write(val);
}

/* Read one byte from the PS/2 data port */
static uint8_t ps2_read(void)
{
    ps2_wait_read();
    return inb(PS2_DATA);
}

/* ---------- IRQ12 handler ---------- */

static void mouse_irq_handler(interrupt_frame_t *frame)
{
    (void)frame;

    /* Discard if no data (spurious) */
    if (!(inb(PS2_STATUS) & PS2_STATUS_OBF)) return;

    uint8_t byte = inb(PS2_DATA);

    /* Re-synchronise: byte 0 must have bit 3 set */
    if (packet_idx == 0 && !(byte & 0x08)) {
        serial_puts("[MOUSE] sync lost, skipping byte\n");
        return;
    }

    packet[packet_idx++] = byte;

    if (packet_idx < 3) return;   /* not a complete packet yet */
    packet_idx = 0;

    /* Decode packet */
    uint8_t  flags = packet[0];
    uint8_t  raw_x = packet[1];
    uint8_t  raw_y = packet[2];

    mouse_event_t ev;
    ev.valid     = true;
    ev.buttons   = flags & 0x07;
    ev.overflow_x = (flags >> 6) & 1;
    ev.overflow_y = (flags >> 7) & 1;

    /* Sign-extend 9-bit values */
    ev.dx = (flags & 0x10) ? (int8_t)(raw_x | 0x100) : (int8_t)raw_x;
    ev.dy = (flags & 0x20) ? (int8_t)(raw_y | 0x100) : (int8_t)raw_y;
    /* Y is inverted in PS/2 (positive = up in hardware, positive = down on screen) */
    ev.dy = -ev.dy;

    /* Store in ring buffer */
    uint32_t next = (mouse_head + 1) % MOUSE_BUF_SIZE;
    if (next != mouse_tail) {
        mouse_buf[mouse_head] = ev;
        mouse_head = next;
    }

    /* Wake any blocked readers */
    for (int i = 0; i < mouse_waiter_count; i++) {
        if (mouse_waiters[i]) {
            sched_unblock(mouse_waiters[i]);
            mouse_waiters[i] = NULL;
        }
    }
    mouse_waiter_count = 0;
}

/* ---------- Public API ---------- */

void mouse_init(void)
{
    /* Enable auxiliary device */
    ps2_cmd(0xA8);

    /* Enable auxiliary device interrupt (bit 1 of byte 0 of config) */
    ps2_cmd(0x20);              /* read config byte */
    uint8_t cfg = ps2_read();
    cfg |= 0x02;                /* enable IRQ12 */
    cfg &= ~0x20;               /* clear "auxiliary device disable" */
    ps2_cmd(0x60);              /* write config byte */
    ps2_write(cfg);

    /* Set defaults */
    mouse_write(0xF6);
    (void)ps2_read();           /* ACK */

    /* Enable mouse */
    mouse_write(0xF4);
    (void)ps2_read();           /* ACK */

    /* Register IRQ12 */
    irq_register(12, mouse_irq_handler);

    serial_puts("[MOUSE] PS/2 mouse initialized (IRQ12)\n");
}

int mouse_get_event(mouse_event_t *ev)
{
    if (!ev) return 0;
    if (mouse_head == mouse_tail) return 0;

    *ev = mouse_buf[mouse_tail];
    mouse_tail = (mouse_tail + 1) % MOUSE_BUF_SIZE;
    return 1;
}

int mouse_read(mouse_event_t *ev)
{
    for (;;) {
        if (mouse_head != mouse_tail) return mouse_get_event(ev);

        uint64_t flags = irq_save();
        if (mouse_head != mouse_tail) {
            irq_restore(flags);
            continue;
        }
        if (current_task && mouse_waiter_count < MOUSE_WAIT_MAX)
            mouse_waiters[mouse_waiter_count++] = current_task;
        sched_block(current_task, UINT64_MAX);
        irq_restore(flags);
    }
}
