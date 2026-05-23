#pragma once
#include "types.h"

/*
 * GDT layout:
 *   Index 0  0x00  null
 *   Index 1  0x08  kernel code  DPL=0  64-bit
 *   Index 2  0x10  kernel data  DPL=0
 *   Index 3  0x18  user data    DPL=3   ← MUST be before user code
 *   Index 4  0x20  user code    DPL=3   64-bit
 *   Index 5  0x28  TSS low      (16-byte system descriptor)
 *   Index 6  0x30  TSS high
 *
 * user_data MUST precede user_code for SYSRET to work:
 *   SYSRET sets CS = STAR[63:48]+16, SS = STAR[63:48]+8
 *   With STAR[63:48] = 0x10:
 *     SS = 0x10+8  = 0x18 = user_data selector ✓
 *     CS = 0x10+16 = 0x20 = user_code selector ✓
 *   (RPL=3 is OR'd in by SYSRET automatically)
 */

#define GDT_NULL        0
#define GDT_KERNEL_CODE 1
#define GDT_KERNEL_DATA 2
#define GDT_USER_DATA   3   /* NOTE: data before code — required by SYSRET */
#define GDT_USER_CODE   4
#define GDT_TSS_LOW     5
#define GDT_TSS_HIGH    6
#define GDT_ENTRIES     7

#define SEL_KERNEL_CODE  (GDT_KERNEL_CODE << 3)        /* 0x08, RPL=0 */
#define SEL_KERNEL_DATA  (GDT_KERNEL_DATA << 3)        /* 0x10, RPL=0 */
#define SEL_USER_DATA    ((GDT_USER_DATA  << 3) | 3)   /* 0x1B, RPL=3 */
#define SEL_USER_CODE    ((GDT_USER_CODE  << 3) | 3)   /* 0x23, RPL=3 */
#define SEL_TSS          (GDT_TSS_LOW << 3)            /* 0x28, RPL=0 */

void gdt_init(void);
void gdt_set_kernel_stack(uint64_t stack_top);
void gdt_set_ist(uint8_t slot, uint64_t stack_top);
