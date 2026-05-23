#include "keyboard.h"
#include "../arch/x86_64/irq.h"
#include "../arch/x86_64/serial.h"
#include "../sched/sched.h"
#include "../include/spinlock.h"

#define KB_DATA     0x60
#define KB_STATUS   0x64
#define KB_BUF_SIZE 256

static volatile uint8_t  kb_buf[KB_BUF_SIZE];
static volatile uint32_t kb_head = 0;
static volatile uint32_t kb_tail = 0;

/*
 * Waiters: tasks blocked in keyboard_read_char().
 * The IRQ handler unblocks them when a key arrives.
 */
#define KB_WAIT_MAX 8
static task_t *kb_waiters[KB_WAIT_MAX];
static int     kb_waiter_count = 0;

static inline uint8_t inb(uint16_t port)
{
    uint8_t v;
    __asm__ __volatile__("inb %1,%0" : "=a"(v) : "Nd"(port));
    return v;
}

static const uint8_t scancode_map[128] = {
    0,   0x1B,'1', '2', '3', '4', '5', '6', '7', '8', '9', '0', '-', '=', '\b',
    '\t','q',  'w', 'e', 'r', 't', 'y', 'u', 'i', 'o', 'p', '[', ']', '\n',
    0,   'a',  's', 'd', 'f', 'g', 'h', 'j', 'k', 'l', ';', '\'','`',
    0,   '\\', 'z', 'x', 'c', 'v', 'b', 'n', 'm', ',', '.', '/', 0,
    '*', 0,   ' ',
};

static const uint8_t scancode_shift_map[128] = {
    0,   0x1B,'!', '@', '#', '$', '%', '^', '&', '*', '(', ')', '_', '+', '\b',
    '\t','Q',  'W', 'E', 'R', 'T', 'Y', 'U', 'I', 'O', 'P', '{', '}', '\n',
    0,   'A',  'S', 'D', 'F', 'G', 'H', 'J', 'K', 'L', ':', '"', '~',
    0,   '|',  'Z', 'X', 'C', 'V', 'B', 'N', 'M', '<', '>', '?', 0,
    '*', 0,   ' ',
};

static bool shift_state = false;
static bool caps_state  = false;

static void kb_irq_handler(interrupt_frame_t *frame)
{
    (void)frame;
    uint8_t sc = inb(KB_DATA);

    if (sc & 0x80) {
        sc &= 0x7F;
        if (sc == 0x2A || sc == 0x36) shift_state = false;
        return;
    }

    if (sc == 0x2A || sc == 0x36) { shift_state = true;           return; }
    if (sc == 0x3A)                { caps_state = !caps_state;    return; }
    if (sc >= 128) return;

    uint8_t c = (shift_state || caps_state) ? scancode_shift_map[sc]
                                            : scancode_map[sc];
    if (!c) return;

    uint32_t next = (kb_head + 1) % KB_BUF_SIZE;
    if (next != kb_tail) {
        kb_buf[kb_head] = c;
        kb_head = next;
    }

    /* Unblock waiting readers */
    for (int i = 0; i < kb_waiter_count; i++) {
        if (kb_waiters[i]) {
            sched_unblock(kb_waiters[i]);
            kb_waiters[i] = NULL;
        }
    }
    kb_waiter_count = 0;
}

void keyboard_init(void)
{
    while (inb(KB_STATUS) & 1) inb(KB_DATA);
    irq_register(1, kb_irq_handler);
    serial_printf("[KB] PS/2 keyboard initialized\n");
}

int keyboard_poll(void)
{
    if (kb_head == kb_tail) return -1;
    uint8_t c = kb_buf[kb_tail];
    kb_tail = (kb_tail + 1) % KB_BUF_SIZE;
    return (int)c;
}

int keyboard_read_char(void)
{
    for (;;) {
        if (kb_head != kb_tail) return keyboard_poll();

        /* Block until IRQ delivers a character */
        uint64_t flags = irq_save();
        if (kb_head != kb_tail) {
            irq_restore(flags);
            continue;
        }
        if (current_task && kb_waiter_count < KB_WAIT_MAX)
            kb_waiters[kb_waiter_count++] = current_task;
        /* Block indefinitely — IRQ will unblock */
        sched_block(current_task, UINT64_MAX);
        irq_restore(flags);
    }
}
