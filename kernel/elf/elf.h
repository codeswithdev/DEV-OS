#pragma once
#include "../include/types.h"
#include "../sched/sched.h"

/*
 * DevOS ELF64 Loader
 *
 * Loads a statically-linked ELF64 executable from a buffer in kernel memory.
 * Maps PT_LOAD segments into a new process address space.
 * Creates a user stack.
 * Creates and enqueues a user task.
 *
 * Returns the new task_t* on success, NULL on error.
 *
 * User virtual address space layout:
 *   0x0000000000400000  ELF load base (typical)
 *   0x00007FFFFFFFFFFF  top of user space
 *   0x00007FFFFFFF0000  user stack top (grows down)
 */

#define USER_STACK_TOP    0x00007FFFFFFF0000ULL
#define USER_STACK_SIZE   (64 * 1024)   /* 64KB initial stack */

task_t *elf_load(const char *name, const void *elf_data, size_t elf_size,
                 uint32_t priority);
