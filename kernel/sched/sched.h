#pragma once
#include "../include/types.h"

typedef enum {
    TASK_CREATED  = 0,
    TASK_READY    = 1,
    TASK_RUNNING  = 2,
    TASK_BLOCKED  = 3,
    TASK_ZOMBIE   = 4,
    TASK_DEAD     = 5,
} task_state_t;

typedef struct {
    uint64_t r15;
    uint64_t r14;
    uint64_t r13;
    uint64_t r12;
    uint64_t rbx;
    uint64_t rbp;
    uint64_t rsp;
} cpu_state_t;

#define MAX_FDS 64

typedef struct {
    void *files[MAX_FDS];
} fd_table_t;

/*
 * task_t — per-task kernel state.
 *
 * Field offsets (must match sched_switch.asm and ring3.asm):
 *   cpu_state_t  @ 40   (TASK_CPU_OFFSET)
 *   kernel_stack @ 96   (TASK_KSTACK_OFFSET)
 *   user_stack   @ 104
 *   rip_entry    @ 112
 *   cr3          @ 120  (TASK_CR3_OFFSET)
 */
typedef struct task {
    uint32_t        pid;            /* 0 */
    uint32_t        ppid;           /* 4 */
    char            name[32];       /* 8 */

    cpu_state_t     cpu;            /* 40 — 56 bytes */

    uint64_t        kernel_stack;   /* 96  — TSS RSP0 */
    uint64_t        user_stack;     /* 104 */
    uint64_t        rip_entry;      /* 112 */

    uint64_t        cr3;            /* 120 */

    task_state_t    state;          /* 128 */
    uint32_t        priority;       /* 132 */
    uint64_t        time_slice;     /* 136 */
    uint64_t        total_ticks;    /* 144 */
    uint64_t        wake_tick;      /* 152 */

    /* Per-process mmap cursor (fixes shared static bug) */
    uint64_t        mmap_cursor;    /* 160 */

    /* AI extension slots */
    uint64_t        ai_weight;
    uint64_t        tensor_mem;
    uint32_t        numa_node;

    struct task    *next;
    struct task    *prev;

    fd_table_t      fds;
} task_t;

#define TASK_CPU_OFFSET      40
#define TASK_KSTACK_OFFSET   96
#define TASK_CR3_OFFSET      120

STATIC_ASSERT(sizeof(task_t) < 4096, "task_t larger than one page");
STATIC_ASSERT(__builtin_offsetof(task_t, cpu)          == TASK_CPU_OFFSET,   "cpu offset");
STATIC_ASSERT(__builtin_offsetof(task_t, kernel_stack) == TASK_KSTACK_OFFSET,"kstack offset");
STATIC_ASSERT(__builtin_offsetof(task_t, cr3)          == TASK_CR3_OFFSET,   "cr3 offset");

extern task_t *idle_task;
extern task_t *current_task;

void     sched_init(void);
task_t  *sched_create_task(const char *name, void (*entry)(void),
                            uint32_t priority);
task_t  *sched_create_user_task(const char *name, uint64_t entry_rip,
                                 uint64_t user_rsp, uint64_t pml4_phys,
                                 uint32_t priority);
void     sched_enqueue(task_t *task);
void     sched_tick(void);
void     sched_yield(void);
void     sched_block(task_t *task, uint64_t wake_tick);
void     sched_unblock(task_t *task);
void     sched_check_sleepers(uint64_t tick);
void     sched_exit(void) NORETURN;

extern void sched_switch(task_t *from, task_t *to);
