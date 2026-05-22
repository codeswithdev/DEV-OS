#include "proc.h"
#include "../mm/heap.h"
#include "../lib/string.h"
#include "../arch/x86_64/serial.h"
#include "../include/panic.h"
#include "../include/spinlock.h"
#include "../include/errno.h"

static proc_t      *proc_table[PROC_MAX];
/* proc_lock: reserved for SMP */ // static spinlock_t proc_lock = SPINLOCK_INIT;

/* Wait queues: one per PID, tasks blocked waiting for that PID to exit */
#define WAIT_QUEUE_MAX 8
typedef struct {
    task_t *tasks[WAIT_QUEUE_MAX];
    int     count;
} wait_queue_t;

static wait_queue_t wait_queues[PROC_MAX];

void proc_init(void)
{
    memset(proc_table,  0, sizeof(proc_table));
    memset(wait_queues, 0, sizeof(wait_queues));
    serial_printf("[PROC] init OK\n");
}

proc_t *proc_create(const char *name, task_t *t)
{
    if (!t) return NULL;
    uint32_t pid = t->pid;
    if (pid >= PROC_MAX) return NULL;

    proc_t *p = (proc_t *)kzalloc(sizeof(proc_t));
    if (!p) return NULL;

    p->pid  = pid;
    p->ppid = t->ppid;
    p->task = t;
    strncpy(p->name, name, 31);

    uint64_t flags = irq_save();
    proc_table[pid] = p;
    irq_restore(flags);

    return p;
}

proc_t *proc_get(uint32_t pid)
{
    if (pid >= PROC_MAX) return NULL;
    return proc_table[pid];
}

void proc_exit(proc_t *p, int code)
{
    if (!p) return;
    p->exit_code = code;
    p->exited    = true;

    /* Wake all tasks waiting on this PID */
    wait_queue_t *wq = &wait_queues[p->pid];
    uint64_t flags = irq_save();
    for (int i = 0; i < wq->count; i++) {
        if (wq->tasks[i])
            sched_unblock(wq->tasks[i]);
    }
    wq->count = 0;
    irq_restore(flags);
}

void proc_reap(uint32_t pid)
{
    if (pid >= PROC_MAX) return;
    uint64_t flags = irq_save();
    if (proc_table[pid]) {
        kfree(proc_table[pid]);
        proc_table[pid] = NULL;
    }
    irq_restore(flags);
}

int proc_waitpid(uint32_t pid, int *status)
{
    proc_t *p = proc_get(pid);
    if (!p) return -(int)ESRCH;

    if (!p->exited) {
        wait_queue_t *wq = &wait_queues[pid];
        uint64_t flags = irq_save();
        if (wq->count < WAIT_QUEUE_MAX)
            wq->tasks[wq->count++] = current_task;
        /* Block until proc_exit() unblocks us */
        sched_block(current_task, UINT64_MAX);
        irq_restore(flags);
    }

    if (status) *status = p->exit_code;
    proc_reap(pid);
    return (int)pid;
}
