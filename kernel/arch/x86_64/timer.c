/*
 * DevOS — PIT Timer Driver
 * Programs PIT channel 0 to 1000 Hz and drives scheduler preemption.
 */

#include "timer.h"
#include "irq.h"
#include "serial.h"
#include "../../include/types.h"
#include "../../sched/sched.h"

#define PIT_CHANNEL0    0x40
#define PIT_CMD         0x43
#define PIT_FREQUENCY   1193182UL
#define TICKS_PER_SEC   1000

static volatile uint64_t tick_count = 0;

static inline void outb(uint16_t p, uint8_t v) {
    __asm__ __volatile__("outb %0,%1" :: "a"(v), "Nd"(p) : "memory");
}

static void timer_irq_handler(interrupt_frame_t *frame)
{
    (void)frame;
    tick_count++;
    sched_tick();
}

void timer_init(void)
{
    uint32_t divisor = (uint32_t)(PIT_FREQUENCY / TICKS_PER_SEC);

    outb(PIT_CMD, 0x36);
    outb(PIT_CHANNEL0, (uint8_t)(divisor & 0xFF));
    outb(PIT_CHANNEL0, (uint8_t)((divisor >> 8) & 0xFF));

    irq_register(0, timer_irq_handler);

    serial_printf("[TIMER] PIT @ %u Hz (divisor=%u)\n",
                  TICKS_PER_SEC, divisor);
}

uint64_t timer_ticks(void)
{
    return tick_count;
}

void timer_sleep_ms(uint32_t ms)
{
    uint64_t target = tick_count + ms;
    while (tick_count < target) {
        __asm__ __volatile__("pause");
    }
}
