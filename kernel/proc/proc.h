#pragma once
#include "../include/types.h"
#include "../include/errno.h"
#include "../sched/sched.h"


#define PROC_MAX    256

typedef struct proc {
    uint32_t    pid;
    uint32_t    ppid;
    task_t     *task;
    int         exit_code;
    bool        exited;
    char        name[32];
} proc_t;

void    proc_init(void);
proc_t *proc_create(const char *name, task_t *t);
proc_t *proc_get(uint32_t pid);
void    proc_exit(proc_t *p, int code);
void    proc_reap(uint32_t pid);
int     proc_waitpid(uint32_t pid, int *status);
