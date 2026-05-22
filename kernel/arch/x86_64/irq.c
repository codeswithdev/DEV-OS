#include "irq.h"
#include "idt.h"
#include "serial.h"

#define PIC1_CMD    0x20
#define PIC1_DATA   0x21
#define PIC2_CMD    0xA0
#define PIC2_DATA   0xA1
#define PIC_EOI     0x20

#define ICW1_ICW4   0x01
#define ICW1_INIT   0x10
#define ICW4_8086   0x01

#define IRQ_BASE    32  /* IRQs remapped to IDT 32-47 */

static inline void outb(uint16_t p, uint8_t v) {
    __asm__ __volatile__("outb %0,%1" :: "a"(v), "Nd"(p) : "memory");
}
static inline void io_wait(void) { outb(0x80, 0); }

static isr_handler_t irq_handlers[16];

void pic_init(void)
{
    /* ICW1 */
    outb(PIC1_CMD,  ICW1_INIT | ICW1_ICW4); io_wait();
    outb(PIC2_CMD,  ICW1_INIT | ICW1_ICW4); io_wait();
    /* ICW2: remap */
    outb(PIC1_DATA, IRQ_BASE);      io_wait();
    outb(PIC2_DATA, IRQ_BASE + 8);  io_wait();
    /* ICW3 */
    outb(PIC1_DATA, 0x04); io_wait();
    outb(PIC2_DATA, 0x02); io_wait();
    /* ICW4 */
    outb(PIC1_DATA, ICW4_8086); io_wait();
    outb(PIC2_DATA, ICW4_8086); io_wait();

    /* Mask all except IRQ2 (cascade) on PIC2; unmask all on PIC1 */
    outb(PIC1_DATA, 0xFB);  /* enable all PIC1 except cascade slot... */
    outb(PIC2_DATA, 0xFF);  /* mask all PIC2 initially */
}

void pic_eoi(uint8_t irq)
{
    if (irq >= 8) outb(PIC2_CMD, PIC_EOI);
    outb(PIC1_CMD, PIC_EOI);
}

static void irq_dispatch(interrupt_frame_t *frame)
{
    uint8_t irq = (uint8_t)(frame->vec - IRQ_BASE);

    if (irq_handlers[irq]) irq_handlers[irq](frame);
    else serial_printf("[IRQ] Spurious IRQ%u\n", irq);

    pic_eoi(irq);
}

void irq_register(uint8_t irq, isr_handler_t h)
{
    if (irq >= 16) return;
    irq_handlers[irq] = h;
    idt_set_handler((uint8_t)(IRQ_BASE + irq), irq_dispatch);

    /* Unmask the IRQ in PIC */
    uint16_t port = (irq < 8) ? PIC1_DATA : PIC2_DATA;
    uint8_t  bit  = (irq < 8) ? irq : (uint8_t)(irq - 8);
    uint8_t  mask;
    __asm__ __volatile__("inb %1,%0" : "=a"(mask) : "Nd"(port));
    mask &= ~(1u << bit);
    outb(port, mask);
}

void irq_unregister(uint8_t irq)
{
    if (irq >= 16) return;
    irq_handlers[irq] = NULL;
    /* Re-mask */
    uint16_t port = (irq < 8) ? PIC1_DATA : PIC2_DATA;
    uint8_t  bit  = (irq < 8) ? irq : (uint8_t)(irq - 8);
    uint8_t  mask;
    __asm__ __volatile__("inb %1,%0" : "=a"(mask) : "Nd"(port));
    mask |= (1u << bit);
    outb(port, mask);
}
