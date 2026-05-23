#include "sched.h"
#include "../mm/heap.h"
#include "../mm/vmm.h"
#include "../mm/pmm.h"
#include "../arch/x86_64/serial.h"
#include "../arch/x86_64/gdt.h"
#include "../include/spinlock.h"
#include "../include/panic.h"
#include "../lib/string.h"

extern void sched_switch(task_t *from, task_t *to);

task_t *idle_task    = NULL;
task_t *current_task = NULL;

static task_t      *run_queue_head = NULL;
static uint32_t     next_pid       = 1;
static uint64_t     sched_tick_count = 0;
/* sched_lock: reserved for SMP — unused in single-CPU build */ // static spinlock_t sched_lock = SPINLOCK_INIT;

/*
 * in_sched: re-entrancy guard.
 * sched_tick() is called from IRQ. If a voluntary yield/block is in
 * progress (inside sched_do_switch), sched_tick must not recurse.
 */
static volatile int in_sched = 0;

#define DEFAULT_TIMESLICE   20
#define KSTACK_SIZE         (16 * 1024)

/* ---- Run queue (circular doubly-linked list) ---- */

static void run_queue_add(task_t *t)
{
    if (!run_queue_head) {
        t->next = t;
        t->prev = t;
        run_queue_head = t;
    } else {
        task_t *tail = run_queue_head->prev;
        tail->next           = t;
        t->prev              = tail;
        t->next              = run_queue_head;
        run_queue_head->prev = t;
    }
    t->state = TASK_READY;
}

static void run_queue_remove(task_t *t)
{
    if (t->next == t) {
        run_queue_head = NULL;
    } else {
        t->prev->next = t->next;
        t->next->prev = t->prev;
        if (run_queue_head == t) run_queue_head = t->next;
    }
    t->next = t->prev = NULL;
}

/* ---- Task allocation ---- */

static task_t *sched_alloc_task(const char *name, uint32_t priority)
{
    task_t *t = (task_t *)kzalloc(sizeof(task_t));
    if (!t) return NULL;

    t->pid        = next_pid++;
    t->ppid       = current_task ? current_task->pid : 0;
    t->priority   = priority;
    t->state      = TASK_CREATED;
    t->time_slice = DEFAULT_TIMESLICE;
    t->next = t->prev = NULL;

    size_t n = 0;
    while (name[n] && n < 31) n++;
    memcpy(t->name, name, n);
    t->name[n] = '\0';

    void *kstack = kmalloc(KSTACK_SIZE);
    if (!kstack) { kfree(t); return NULL; }

    t->kernel_stack = ALIGN_DOWN((uint64_t)(uintptr_t)kstack + KSTACK_SIZE, 16);

    uint64_t cr3;
    __asm__ __volatile__("mov %%cr3, %0" : "=r"(cr3));
    t->cr3 = cr3;

    return t;
}

task_t *sched_create_task(const char *name, void (*entry)(void),
                           uint32_t priority)
{
    task_t *t = sched_alloc_task(name, priority);
    if (!t) return NULL;

    t->rip_entry = (uint64_t)(uintptr_t)entry;

    /*
     * Initial kernel stack layout (RSP points at entry_fn):
     *   [RSP+0]  = entry_fn      ← sched_switch 'ret' pops this
     *   [RSP+8]  = sched_exit    ← entry_fn's ret target
     */
    uint64_t *sp = (uint64_t *)t->kernel_stack;
    *--sp = (uint64_t)(uintptr_t)sched_exit;
    *--sp = (uint64_t)(uintptr_t)entry;

    t->cpu.r15 = t->cpu.r14 = t->cpu.r13 = 0;
    t->cpu.r12 = t->cpu.rbx = t->cpu.rbp = 0;
    t->cpu.rsp = (uint64_t)(uintptr_t)sp;

    serial_printf("[SCHED] task '%s' pid=%u entry=0x%llx\n",
                  t->name, t->pid, t->rip_entry);
    return t;
}

task_t *sched_create_user_task(const char *name, uint64_t entry_rip,
                                uint64_t user_rsp, uint64_t pml4_phys,
                                uint32_t priority)
{
    task_t *t = sched_alloc_task(name, priority);
    if (!t) return NULL;

    t->rip_entry  = entry_rip;
    t->user_stack = user_rsp;
    t->cr3        = pml4_phys;

    extern void ring3_enter_first(void);
    uint64_t *sp = (uint64_t *)t->kernel_stack;
    *--sp = (uint64_t)(uintptr_t)sched_exit;
    *--sp = (uint64_t)(uintptr_t)ring3_enter_first;

    t->cpu.r15 = t->cpu.r14 = t->cpu.r13 = 0;
    t->cpu.r12 = t->cpu.rbx = t->cpu.rbp = 0;
    t->cpu.rsp = (uint64_t)(uintptr_t)sp;

    /* Initialize mmap cursor per-process */
    t->mmap_cursor = 0x0000500000000000ULL;

    serial_printf("[SCHED] user task '%s' pid=%u rip=0x%llx usp=0x%llx\n",
                  t->name, t->pid, entry_rip, user_rsp);
    return t;
}

void sched_enqueue(task_t *t)
{
    run_queue_add(t);
}

/* ---- Idle task ---- */

static void idle_fn(void)
{
    for (;;) __asm__ __volatile__("hlt");
}

/* ---- Init ---- */

void sched_init(void)
{
    task_t *boot = (task_t *)kzalloc(sizeof(task_t));
    if (!boot) PANIC("cannot allocate boot task");

    boot->pid        = 0;
    boot->state      = TASK_RUNNING;
    boot->priority   = 255;
    boot->time_slice = 1;
    memcpy(boot->name, "boot", 5);

    __asm__ __volatile__("mov %%cr3, %0" : "=r"(boot->cr3));

    uint64_t rsp;
    __asm__ __volatile__("mov %%rsp, %0" : "=r"(rsp));
    boot->kernel_stack = ALIGN_UP(rsp, PAGE_SIZE) + PAGE_SIZE;

    current_task = boot;

    idle_task = sched_create_task("idle", idle_fn, 255);
    if (!idle_task) PANIC("cannot create idle task");
    run_queue_add(idle_task);

    serial_printf("[SCHED] init OK\n");
}

/* ---- Scheduling ---- */

static task_t *sched_pick_next(void)
{
    if (!run_queue_head) return idle_task;

    task_t *start = run_queue_head;
    task_t *t     = start;
    do {
        if (t->state == TASK_READY) return t;
        t = t->next;
    } while (t != start);

    return idle_task;
}

/*
 * sched_do_switch — core context switch.
 * MUST be called with interrupts disabled.
 */
static void sched_do_switch(task_t *next)
{
    if (next == current_task) return;

    task_t *prev = current_task;

    if (prev->state == TASK_RUNNING) {
        prev->state = TASK_READY;
        /* Never re-enqueue the boot-time task shell or idle via this path */
        if (prev != idle_task)
            run_queue_add(prev);
    }

    run_queue_remove(next);
    next->state      = TASK_RUNNING;
    next->time_slice = DEFAULT_TIMESLICE;
    current_task     = next;

    gdt_set_kernel_stack(next->kernel_stack);

    extern uint64_t syscall_kernel_rsp;
    syscall_kernel_rsp = next->kernel_stack;

    in_sched = 0;   /* clear before switch so the new task sees it clean */
    sched_switch(prev, next);
    /* Execution resumes here when prev is scheduled back in */
}

/* ---- Timer tick (called from IRQ, interrupts disabled) ---- */

void sched_tick(void)
{
    sched_tick_count++;

    sched_check_sleepers(sched_tick_count);

    if (!current_task) return;
    if (in_sched) return;   /* voluntary switch in progress — skip preemption */

    if (current_task->time_slice > 0) {
        current_task->time_slice--;
        current_task->total_ticks++;
    }

    if (current_task->time_slice == 0) {
        in_sched = 1;
        task_t *next = sched_pick_next();
        sched_do_switch(next);
    }
}

/* ---- Voluntary yield ---- */

void sched_yield(void)
{
    uint64_t flags = irq_save();
    in_sched = 1;
    task_t *next = sched_pick_next();
    sched_do_switch(next);
    irq_restore(flags);
}

/* ---- Sleep list ---- */

#define SLEEP_LIST_MAX 256
static task_t *sleep_list[SLEEP_LIST_MAX];
static int     sleep_list_cnt = 0;

void sched_block(task_t *t, uint64_t wake_tick)
{
    uint64_t flags = irq_save();

    t->state     = TASK_BLOCKED;
    t->wake_tick = wake_tick;
    run_queue_remove(t);

    if (sleep_list_cnt < SLEEP_LIST_MAX)
        sleep_list[sleep_list_cnt++] = t;

    if (t == current_task) {
        in_sched = 1;
        task_t *next = sched_pick_next();
        sched_do_switch(next);
    }

    irq_restore(flags);
}

void sched_unblock(task_t *t)
{
    if (t->state == TASK_BLOCKED)
        run_queue_add(t);
}

void sched_check_sleepers(uint64_t tick)
{
    for (int i = 0; i < sleep_list_cnt; i++) {
        task_t *t = sleep_list[i];
        if (t && t->state == TASK_BLOCKED && tick >= t->wake_tick) {
            sched_unblock(t);
            sleep_list[i] = sleep_list[--sleep_list_cnt];
            i--;
        }
    }
}

/* ---- Exit ---- */

void sched_exit(void)
{
    uint64_t flags = irq_save();

    current_task->state = TASK_ZOMBIE;
    run_queue_remove(current_task);

    serial_printf("[SCHED] task '%s' pid=%u exited\n",
                  current_task->name, current_task->pid);

    task_t *dead = current_task;
    task_t *next = sched_pick_next();

    /*
     * We must not return to 'dead' after this switch.
     * Set current_task to next BEFORE calling sched_switch so that
     * if a timer fires during the switch, it sees a valid current_task.
     */
    next->state  = TASK_RUNNING;
    current_task = next;

    gdt_set_kernel_stack(next->kernel_stack);
    extern uint64_t syscall_kernel_rsp;
    syscall_kernel_rsp = next->kernel_stack;

    /* irq_restore is intentionally skipped — we never return */
    (void)flags;
    sched_switch(dead, next);

    __asm__ __volatile__("ud2");
    __builtin_unreachable();
}
<<<<<<< HEAD

task_t *sched_get_run_queue_head(void)
{
    return run_queue_head;
}

int sched_get_sleep_list(task_t **out, int max)
{
    int n = sleep_list_cnt < max ? sleep_list_cnt : max;
    for (int i = 0; i < n; i++) out[i] = sleep_list[i];
    return n;
}
=======
>>>>>>> 86b48d9e005102ecf781f5f192fd54d487851616
