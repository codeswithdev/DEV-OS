#include "idt.h"
#include "serial.h"
#include "../../include/types.h"

typedef struct {
    uint16_t off_low;
    uint16_t cs;
    uint8_t  ist;
    uint8_t  attr;
    uint16_t off_mid;
    uint32_t off_high;
    uint32_t reserved;
} PACKED idt_entry_t;

static idt_entry_t idt[256] ALIGNED(16);
static struct { uint16_t limit; uint64_t base; } PACKED idtr;

extern uint64_t isr_stub_table[256];
static isr_handler_t handlers[256];

static void idt_set_entry(uint8_t vec, uint64_t handler, uint8_t ist, uint8_t attr)
{
    idt[vec].off_low  = (uint16_t)(handler & 0xFFFF);
    idt[vec].cs       = 0x08; /* kernel CS */
    idt[vec].ist      = ist;
    idt[vec].attr     = attr;
    idt[vec].off_mid  = (uint16_t)((handler >> 16) & 0xFFFF);
    idt[vec].off_high = (uint32_t)(handler >> 32);
    idt[vec].reserved = 0;
}

void idt_set_handler(uint8_t vec, isr_handler_t h)
{
    handlers[vec] = h;
}

static const char *exception_names[32] = {
    "#DE", "#DB", "NMI", "#BP", "#OF", "#BR", "#UD", "#NM",
    "#DF", "CSO", "#TS", "#NP", "#SS", "#GP", "#PF", "RSV",
    "#MF", "#AC", "#MC", "#XF", "#VE", "#CP", "RSV", "RSV",
    "RSV", "RSV", "RSV", "RSV", "RSV", "RSV", "#SX", "RSV"
};

void isr_dispatch(interrupt_frame_t *frame)
{
    uint64_t vec = frame->vec;

    if (handlers[vec]) {
        handlers[vec](frame);
        return;
    }

    /* Unhandled exception */
    if (vec < 32) {
        serial_printf("\n[PANIC] Unhandled exception %llu %s\n",
                      vec, exception_names[vec]);
        serial_printf("  RIP=0x%016llx  CS=0x%04llx  RFLAGS=0x%08llx\n",
                      frame->rip, frame->cs, frame->rflags);
        serial_printf("  RSP=0x%016llx  SS=0x%04llx  ERR=0x%08llx\n",
                      frame->rsp, frame->ss, frame->err);
        serial_printf("  RAX=0x%016llx  RBX=0x%016llx  RCX=0x%016llx\n",
                      frame->rax, frame->rbx, frame->rcx);
        serial_printf("  RDX=0x%016llx  RSI=0x%016llx  RDI=0x%016llx\n",
                      frame->rdx, frame->rsi, frame->rdi);
        for (;;) __asm__ __volatile__("cli; hlt");
    }
    /* Unknown IRQ — ignore */
}

void idt_init(void)
{
    for (int i = 0; i < 256; i++) {
        /* All gates: 64-bit interrupt gate (0x8E), IST=0 */
        idt_set_entry((uint8_t)i, isr_stub_table[i], 0, 0x8E);
    }

    idtr.limit = (uint16_t)(sizeof(idt) - 1);
    idtr.base  = (uint64_t)(uintptr_t)idt;
    __asm__ __volatile__("lidt %0" :: "m"(idtr) : "memory");
}
