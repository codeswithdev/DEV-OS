#pragma once
#include "../include/errno.h"
#include "../include/types.h"

/* Linux-compatible syscall numbers (subset) */
#define SYS_READ          0
#define SYS_WRITE         1
#define SYS_OPEN          2
#define SYS_CLOSE         3
#define SYS_LSEEK         8
#define SYS_MMAP          9
#define SYS_MUNMAP        11
#define SYS_EXIT          60
#define SYS_GETPID        39
#define SYS_YIELD         24
#define SYS_NANOSLEEP     35
#define SYS_MKDIR         83
#define SYS_UNLINK        87
#define SYS_STAT          4
#define SYS_GETTIME       228

#define SYSCALL_MAX       256



typedef uint64_t (*syscall_fn_t)(uint64_t, uint64_t, uint64_t,
                                  uint64_t, uint64_t, uint64_t);

extern syscall_fn_t syscall_table[SYSCALL_MAX];
extern uint64_t     syscall_kernel_rsp;

void syscall_init(void);
extern void syscall_entry(void);

/* Handler declarations */
uint64_t sys_read(uint64_t, uint64_t, uint64_t, uint64_t, uint64_t, uint64_t);
uint64_t sys_write(uint64_t, uint64_t, uint64_t, uint64_t, uint64_t, uint64_t);
uint64_t sys_open(uint64_t, uint64_t, uint64_t, uint64_t, uint64_t, uint64_t);
uint64_t sys_close(uint64_t, uint64_t, uint64_t, uint64_t, uint64_t, uint64_t);
uint64_t sys_lseek(uint64_t, uint64_t, uint64_t, uint64_t, uint64_t, uint64_t);
uint64_t sys_mmap(uint64_t, uint64_t, uint64_t, uint64_t, uint64_t, uint64_t);
uint64_t sys_munmap(uint64_t, uint64_t, uint64_t, uint64_t, uint64_t, uint64_t);
uint64_t sys_exit(uint64_t, uint64_t, uint64_t, uint64_t, uint64_t, uint64_t);
uint64_t sys_getpid(uint64_t, uint64_t, uint64_t, uint64_t, uint64_t, uint64_t);
uint64_t sys_yield(uint64_t, uint64_t, uint64_t, uint64_t, uint64_t, uint64_t);
uint64_t sys_nanosleep(uint64_t, uint64_t, uint64_t, uint64_t, uint64_t, uint64_t);
uint64_t sys_mkdir(uint64_t, uint64_t, uint64_t, uint64_t, uint64_t, uint64_t);
uint64_t sys_gettime(uint64_t, uint64_t, uint64_t, uint64_t, uint64_t, uint64_t);
