/*
 * DevOS — Local APIC driver
 *
 * The Local APIC is accessed via MMIO.  Default base is 0xFEE00000
 * but ACPI MADT may report a different address.
 *
 * We operate in xAPIC mode (not x2APIC).  The APIC timer is initialised
 * in one-shot mode and can be used as an alternative to the PIT once the
 * frequency is calibrated.  For now, the PIT remains the tick source and
 * the APIC timer is prepared but not active (SMP future use).
 *
 * PIC fallback: if APIC is not detected (CPUID check fails), the existing
 * PIC-based IRQ infrastructure remains untouched.
 */
#include "apic.h"
#include "serial.h"
#include "../../include/types.h"
#include "../../mm/vmm.h"

/* ---- APIC register offsets (byte-indexed from APIC_BASE) ---- */
#define APIC_ID             0x020
#define APIC_VER            0x030
#define APIC_TPR            0x080   /* Task Priority Register */
#define APIC_EOI            0x0B0   /* End Of Interrupt */
#define APIC_LDR            0x0D0   /* Logical Destination Register */
#define APIC_DFR            0x0E0   /* Destination Format Register */
#define APIC_SVR            0x0F0   /* Spurious Interrupt Vector Register */
#define APIC_ICR_LOW        0x300   /* Interrupt Command Register low */
#define APIC_ICR_HIGH       0x310
#define APIC_LVT_TIMER      0x320
#define APIC_LVT_LINT0      0x350
#define APIC_LVT_LINT1      0x360
#define APIC_LVT_ERR        0x370
#define APIC_TIMER_INIT     0x380
#define APIC_TIMER_CURR     0x390
#define APIC_TIMER_DIV      0x3E0

#define APIC_SVR_ENABLE     (1 << 8)
#define APIC_LVT_MASKED     (1 << 16)
#define APIC_SPURIOUS_VEC   0xFF

/* MSR for APIC base address */
#define MSR_APIC_BASE       0x1B
#define APIC_BASE_ENABLE    (1ULL << 11)

static volatile uint32_t *apic_base = NULL;
static bool               apic_ready = false;

/* ---- CPUID helper ---- */
static inline uint32_t cpuid_edx_bit(uint32_t leaf, uint32_t bit)
{
    uint32_t eax, ebx, ecx, edx;
    __asm__ __volatile__(
        "cpuid"
        : "=a"(eax), "=b"(ebx), "=c"(ecx), "=d"(edx)
        : "a"(leaf), "c"(0)
    );
    (void)eax; (void)ebx; (void)ecx;
    return (edx >> bit) & 1;
}

/* ---- MSR access ---- */
static inline uint64_t rdmsr_apic(uint32_t msr)
{
    uint32_t lo, hi;
    __asm__ __volatile__("rdmsr" : "=a"(lo), "=d"(hi) : "c"(msr));
    return ((uint64_t)hi << 32) | lo;
}
static inline void wrmsr_apic(uint32_t msr, uint64_t val)
{
    __asm__ __volatile__("wrmsr"
        :: "c"(msr), "a"((uint32_t)val), "d"((uint32_t)(val >> 32)));
}

/* ---- APIC MMIO read/write ---- */
static inline uint32_t apic_read(uint32_t reg)
{
    return apic_base[reg >> 2];
}
static inline void apic_write(uint32_t reg, uint32_t val)
{
    apic_base[reg >> 2] = val;
}

/* ---- Public API ---- */

bool apic_available(void)
{
    return apic_ready;
}

uint32_t apic_id(void)
{
    if (!apic_ready) return 0;
    return (apic_read(APIC_ID) >> 24) & 0xFF;
}

void apic_eoi(void)
{
    if (apic_ready) apic_write(APIC_EOI, 0);
}

bool apic_init(void)
{
    /* Check CPUID feature bit 9 (APIC on-chip) */
    if (!cpuid_edx_bit(1, 9)) {
        serial_puts("[APIC] CPUID: no local APIC — using PIC\n");
        return false;
    }

    /* Read IA32_APIC_BASE MSR to get physical base address */
    uint64_t msr_val  = rdmsr_apic(MSR_APIC_BASE);
    uint64_t apic_phys = msr_val & 0xFFFFF000ULL;

    /* Map APIC MMIO into kernel virtual space as MMIO (uncacheable) */
    vmm_map(kernel_pml4, (uint64_t)(uintptr_t)(KERNEL_VIRT_BASE + apic_phys),
            apic_phys, VMM_MMIO);
    apic_base = (volatile uint32_t *)(uintptr_t)(KERNEL_VIRT_BASE + apic_phys);

    /* Enable APIC via MSR (set bit 11) */
    wrmsr_apic(MSR_APIC_BASE, msr_val | APIC_BASE_ENABLE);

    /* Software-enable: set SVR bit 8, spurious vector = 0xFF */
    apic_write(APIC_SVR, APIC_SVR_ENABLE | APIC_SPURIOUS_VEC);

    /* Set TPR to 0 — accept all interrupts */
    apic_write(APIC_TPR, 0);

    /* Mask LINT0, LINT1, timer, error for now */
    apic_write(APIC_LVT_LINT0, APIC_LVT_MASKED);
    apic_write(APIC_LVT_LINT1, APIC_LVT_MASKED);
    apic_write(APIC_LVT_TIMER, APIC_LVT_MASKED);
    apic_write(APIC_LVT_ERR,   APIC_LVT_MASKED | 0xFE);

    apic_ready = true;

    serial_printf("[APIC] Local APIC at 0x%llx id=%u ver=0x%x\n",
                  (uint64_t)apic_phys, apic_id(),
                  apic_read(APIC_VER) & 0xFF);
    return true;
}
