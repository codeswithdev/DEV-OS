#include "syscall.h"
#include "../arch/x86_64/cpu.h"
#include "../arch/x86_64/serial.h"
#include "../arch/x86_64/gdt.h"
#include "../arch/x86_64/timer.h"
#include "../sched/sched.h"
#include "../mm/vmm.h"
#include "../mm/vmm_ext.h"
#include "../mm/heap.h"
#include "../fs/vfs.h"
#include "../include/types.h"
#include "../include/panic.h"

uint64_t syscall_kernel_rsp = 0;
extern void syscall_entry(void);

/* Validate a user pointer range — must be canonical user-space */
static inline int uptr_ok(uint64_t addr, uint64_t len)
{
    if (!addr || len == 0) return 0;
    if (addr >= 0x00007FFFFFFFFFFFULL) return 0;
    if (addr + len < addr) return 0;      /* overflow */
    if (addr + len > 0x00007FFFFFFFFFFFULL) return 0;
    return 1;
}

syscall_fn_t syscall_table[SYSCALL_MAX] = {0};

void syscall_init(void)
{
    uint64_t efer = rdmsr(MSR_EFER);
    wrmsr(MSR_EFER, efer | EFER_SCE);

    /*
     * STAR[47:32] = kernel CS (0x08); SYSRET loads CS = STAR[63:48]+16
     * and SS = STAR[63:48]+8.  With STAR[63:48]=0x10: CS=0x20, SS=0x18.
     */
    wrmsr(MSR_STAR,   ((uint64_t)0x08 << 32) | ((uint64_t)0x10 << 48));
    wrmsr(MSR_LSTAR,  (uint64_t)(uintptr_t)syscall_entry);
    wrmsr(MSR_CSTAR,  0);
    /* Mask: IF(9), TF(8), DF(10), AC(18) */
    wrmsr(MSR_SFMASK, (1u<<9)|(1u<<8)|(1u<<10)|(1u<<18));

    syscall_table[SYS_READ]      = sys_read;
    syscall_table[SYS_WRITE]     = sys_write;
    syscall_table[SYS_OPEN]      = sys_open;
    syscall_table[SYS_CLOSE]     = sys_close;
    syscall_table[SYS_LSEEK]     = sys_lseek;
    syscall_table[SYS_MMAP]      = sys_mmap;
    syscall_table[SYS_MUNMAP]    = sys_munmap;
    syscall_table[SYS_EXIT]      = sys_exit;
    syscall_table[SYS_GETPID]    = sys_getpid;
    syscall_table[SYS_YIELD]     = sys_yield;
    syscall_table[SYS_NANOSLEEP] = sys_nanosleep;
    syscall_table[SYS_MKDIR]     = sys_mkdir;
    syscall_table[SYS_GETTIME]   = sys_gettime;

    if (current_task) syscall_kernel_rsp = current_task->kernel_stack;

    serial_printf("[SYSCALL] ready, LSTAR=0x%p\n",
                  (void *)(uintptr_t)syscall_entry);
}

uint64_t sys_read(uint64_t fd, uint64_t buf, uint64_t count,
                   uint64_t a4, uint64_t a5, uint64_t a6)
{
    (void)a4; (void)a5; (void)a6;
    if (!uptr_ok(buf, count)) return (uint64_t)-EFAULT;
    ssize_t n = vfs_read((int)fd, (void *)(uintptr_t)buf, (size_t)count);
    return (uint64_t)n;
}

uint64_t sys_write(uint64_t fd, uint64_t buf, uint64_t count,
                    uint64_t a4, uint64_t a5, uint64_t a6)
{
    (void)a4; (void)a5; (void)a6;
    if (!uptr_ok(buf, count)) return (uint64_t)-EFAULT;
    if (fd == 1 || fd == 2) {
        const char *s = (const char *)(uintptr_t)buf;
        for (uint64_t i = 0; i < count; i++) serial_putchar(s[i]);
        return count;
    }
    ssize_t n = vfs_write((int)fd, (const void *)(uintptr_t)buf, (size_t)count);
    return (uint64_t)n;
}

uint64_t sys_open(uint64_t path, uint64_t flags, uint64_t mode,
                   uint64_t a4, uint64_t a5, uint64_t a6)
{
    (void)a4; (void)a5; (void)a6;
    if (!uptr_ok(path, 1)) return (uint64_t)-EFAULT;
    int fd = vfs_open((const char *)(uintptr_t)path, (int)flags, (uint32_t)mode);
    return (uint64_t)fd;
}

uint64_t sys_close(uint64_t fd, uint64_t a2, uint64_t a3,
                    uint64_t a4, uint64_t a5, uint64_t a6)
{
    (void)a2; (void)a3; (void)a4; (void)a5; (void)a6;
    return (uint64_t)vfs_close((int)fd);
}

uint64_t sys_lseek(uint64_t fd, uint64_t off, uint64_t whence,
                    uint64_t a4, uint64_t a5, uint64_t a6)
{
    (void)a4; (void)a5; (void)a6;
    return (uint64_t)vfs_lseek((int)fd, (int64_t)off, (int)whence);
}

uint64_t sys_exit(uint64_t code, uint64_t a2, uint64_t a3,
                   uint64_t a4, uint64_t a5, uint64_t a6)
{
    (void)a2; (void)a3; (void)a4; (void)a5; (void)a6;
    serial_printf("[SYSCALL] exit(%llu) pid=%u\n",
                  code, current_task ? current_task->pid : 0);
    sched_exit();
}

uint64_t sys_getpid(uint64_t a1, uint64_t a2, uint64_t a3,
                     uint64_t a4, uint64_t a5, uint64_t a6)
{
    (void)a1; (void)a2; (void)a3; (void)a4; (void)a5; (void)a6;
    return current_task ? current_task->pid : 0;
}

uint64_t sys_yield(uint64_t a1, uint64_t a2, uint64_t a3,
                    uint64_t a4, uint64_t a5, uint64_t a6)
{
    (void)a1; (void)a2; (void)a3; (void)a4; (void)a5; (void)a6;
    sched_yield();
    return 0;
}

uint64_t sys_nanosleep(uint64_t rqtp, uint64_t rmtp, uint64_t a3,
                        uint64_t a4, uint64_t a5, uint64_t a6)
{
    (void)rmtp; (void)a3; (void)a4; (void)a5; (void)a6;
    if (!uptr_ok(rqtp, 16)) return (uint64_t)-EFAULT;
    uint64_t *ts = (uint64_t *)(uintptr_t)rqtp;
    uint64_t ms  = ts[0] * 1000 + ts[1] / 1000000;
    uint64_t wake = timer_ticks() + ms;
    sched_block(current_task, wake);
    return 0;
}

uint64_t sys_mmap(uint64_t addr, uint64_t length, uint64_t prot,
                   uint64_t flags, uint64_t fd, uint64_t offset)
{
    (void)prot; (void)flags; (void)fd; (void)offset;
    if (!length) return (uint64_t)-EINVAL;
    if (!current_task) return (uint64_t)-ESRCH;

    uint64_t va     = addr ? ALIGN_DOWN(addr, PAGE_SIZE)
                           : current_task->mmap_cursor;
    uint64_t nbytes = ALIGN_UP(length, PAGE_SIZE);

    pml4_t pml4 = (pml4_t)(uintptr_t)current_task->cr3;
    if (vmm_alloc_range(pml4, va, nbytes, VMM_USER_RW | VMM_NO_EXEC) != 0)
        return (uint64_t)-ENOMEM;

    if (!addr) current_task->mmap_cursor = va + nbytes;
    return va;
}

uint64_t sys_munmap(uint64_t addr, uint64_t length, uint64_t a3,
                     uint64_t a4, uint64_t a5, uint64_t a6)
{
    (void)a3; (void)a4; (void)a5; (void)a6;
    if (!addr || !length) return (uint64_t)-EINVAL;
    vmm_free_range((pml4_t)(uintptr_t)current_task->cr3, addr, length);
    return 0;
}

uint64_t sys_mkdir(uint64_t path, uint64_t mode, uint64_t a3,
                    uint64_t a4, uint64_t a5, uint64_t a6)
{
    (void)a3; (void)a4; (void)a5; (void)a6;
    if (!uptr_ok(path, 1)) return (uint64_t)-EFAULT;
    return (uint64_t)vfs_mkdir((const char *)(uintptr_t)path, (uint32_t)mode);
}

uint64_t sys_gettime(uint64_t a1, uint64_t a2, uint64_t a3,
                      uint64_t a4, uint64_t a5, uint64_t a6)
{
    (void)a1; (void)a2; (void)a3; (void)a4; (void)a5; (void)a6;
    return timer_ticks();
}
