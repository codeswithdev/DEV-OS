/*
 * DevOS kernel shell — runs as a kernel task in ring 0.
 * Reads from PS/2 keyboard, writes to VGA and serial.
 * Provides a minimal command set for testing and debugging.
 */

#include "shell.h"
#include "../drivers/keyboard.h"
#include "../drivers/vga.h"
#include "../arch/x86_64/serial.h"
#include "../mm/pmm.h"
#include "../mm/heap.h"
#include "../fs/vfs.h"
#include "../sched/sched.h"
#include "../proc/proc.h"
#include "../lib/string.h"
#include "../lib/printf.h"
<<<<<<< HEAD
#include "../arch/x86_64/timer.h"
=======
>>>>>>> 86b48d9e005102ecf781f5f192fd54d487851616

#define SHELL_LINE_MAX 256
#define SHELL_ARGS_MAX 16

static char line_buf[SHELL_LINE_MAX];
static int  line_len = 0;

static void shell_print(const char *s)
{
    vga_puts(s);
    serial_puts(s);
}

static void shell_putchar(char c)
{
    vga_putchar(c);
    serial_putchar((uint8_t)c);
}

static void shell_readline(void)
{
    line_len = 0;
    shell_print("devos> ");

    for (;;) {
        int c = keyboard_read_char();
        if (c < 0) continue;

        if (c == '\n' || c == '\r') {
            shell_putchar('\n');
            line_buf[line_len] = '\0';
            return;
        }
        if (c == '\b') {
            if (line_len > 0) {
                line_len--;
                /* Erase last character on VGA */
                vga_putchar('\b');
                vga_putchar(' ');
                vga_putchar('\b');
                serial_putchar('\b');
            }
            continue;
        }
        if (line_len < SHELL_LINE_MAX - 1) {
            line_buf[line_len++] = (char)c;
            shell_putchar((char)c);
        }
    }
}

/* Split line_buf into argv, return argc */
static int shell_parse(char *argv[], int max_args)
{
    int argc = 0;
    char *p  = line_buf;

    while (*p == ' ') p++;

    while (*p && argc < max_args) {
        argv[argc++] = p;
        while (*p && *p != ' ') p++;
        if (*p) *p++ = '\0';
        while (*p == ' ') p++;
    }
    return argc;
}

/* ---- Built-in commands ---- */

static void cmd_help(void)
{
    shell_print("Commands:\n");
    shell_print("  help       — this message\n");
    shell_print("  clear      — clear screen\n");
    shell_print("  meminfo    — physical memory stats\n");
    shell_print("  heapinfo   — kernel heap stats\n");
    shell_print("  ps         — list tasks\n");
    shell_print("  ls [path]  — list directory\n");
    shell_print("  cat <path> — print file contents\n");
    shell_print("  echo <...> — print arguments\n");
    shell_print("  uptime     — ticks since boot\n");
<<<<<<< HEAD
    shell_print("  halt       — halt the system\n");
    shell_print("  reboot     — reboot the system\n");
=======
>>>>>>> 86b48d9e005102ecf781f5f192fd54d487851616
}

static void cmd_clear(void)
{
    vga_clear();
}

static void cmd_meminfo(void)
{
    char buf[64];
    uint64_t free_mb  = (pmm_free_pages()  * PAGE_SIZE) >> 20;
    uint64_t total_mb = (pmm_total_pages() * PAGE_SIZE) >> 20;
    snprintf(buf, sizeof(buf), "Physical: %llu MB free / %llu MB total\n",
             free_mb, total_mb);
    shell_print(buf);
}

static void cmd_heapinfo(void)
{
    heap_dump_stats();
}

static void cmd_ps(void)
{
<<<<<<< HEAD
    shell_print("PID  STATE    PRIO  NAME\n");
    shell_print("---  -------  ----  ----\n");
=======
    extern task_t *current_task;

    shell_print("PID  STATE    NAME\n");

    /* Walk run queue */
    task_t *t = current_task;
    if (!t) return;
>>>>>>> 86b48d9e005102ecf781f5f192fd54d487851616

    static const char *state_names[] = {
        "CREATED", "READY  ", "RUNNING", "BLOCKED", "ZOMBIE ", "DEAD   "
    };

<<<<<<< HEAD
    char buf[80];

    /* Print current running task */
    task_t *cur = current_task;
    if (cur) {
        snprintf(buf, sizeof(buf), "%-4u %s  %-4u  %s  <running>\n",
                 cur->pid,
                 state_names[cur->state < 6 ? cur->state : 5],
                 cur->priority,
                 cur->name);
        shell_print(buf);
    }

    /* Walk the run queue (circular doubly-linked list) */
    task_t *head = sched_get_run_queue_head();
    if (head) {
        task_t *t = head;
        do {
            if (t != cur) {
                snprintf(buf, sizeof(buf), "%-4u %s  %-4u  %s\n",
                         t->pid,
                         state_names[t->state < 6 ? t->state : 5],
                         t->priority,
                         t->name);
                shell_print(buf);
            }
            t = t->next;
        } while (t && t != head);
    }

    /* Walk sleep list */
    task_t *sleeping[32];
    int ns = sched_get_sleep_list(sleeping, 32);
    for (int i = 0; i < ns; i++) {
        task_t *t = sleeping[i];
        if (t) {
            snprintf(buf, sizeof(buf), "%-4u BLOCKED  %-4u  %s  (sleep)\n",
                     t->pid, t->priority, t->name);
            shell_print(buf);
        }
    }
=======
    /* Print current task */
    char buf[64];
    snprintf(buf, sizeof(buf), "%-4u %s  %s\n",
             t->pid,
             state_names[t->state < 6 ? t->state : 5],
             t->name);
    shell_print(buf);
>>>>>>> 86b48d9e005102ecf781f5f192fd54d487851616
}

static void cmd_ls(const char *path)
{
    if (!path || !*path) path = "/";

    vfs_node_t *dir = vfs_lookup(path);
    if (!dir) {
        shell_print("ls: not found\n");
        return;
    }
    if (!(dir->flags & VFS_TYPE_DIR)) {
        shell_print("ls: not a directory\n");
        return;
    }

    char name[VFS_NAME_MAX + 1];
    for (uint32_t i = 0; ; i++) {
        if (!dir->ops || !dir->ops->readdir) break;
        if (dir->ops->readdir(dir, i, name) != 0) break;
        shell_print(name);
        shell_putchar('\n');
    }
}

static void cmd_cat(const char *path)
{
    if (!path || !*path) {
        shell_print("cat: path required\n");
        return;
    }

    int fd = vfs_open(path, 0, 0);
    if (fd < 0) {
        shell_print("cat: cannot open file\n");
        return;
    }

    char buf[256];
    ssize_t n;
    while ((n = vfs_read(fd, buf, sizeof(buf) - 1)) > 0) {
        buf[n] = '\0';
        shell_print(buf);
    }
    vfs_close(fd);
}

static void cmd_echo(char **argv, int argc)
{
    for (int i = 1; i < argc; i++) {
        if (i > 1) shell_putchar(' ');
        shell_print(argv[i]);
    }
    shell_putchar('\n');
}

static void cmd_uptime(void)
{
<<<<<<< HEAD
    char buf[64];
    uint64_t ticks = timer_ticks();
    snprintf(buf, sizeof(buf), "Uptime: %llu ms (%llu sec)\n",
             ticks, ticks / 1000);
    shell_print(buf);
}

static void cmd_halt(void)
{
    shell_print("Halting system...\n");
    __asm__ __volatile__("cli; hlt");
}

static void cmd_reboot(void)
{
    shell_print("Rebooting...\n");
    /* Pulse the 8042 keyboard controller reset line */
    __asm__ __volatile__(
        "1: inb $0x64, %%al\n"
        "   testb $0x02, %%al\n"
        "   jnz 1b\n"
        "   movb $0xFE, %%al\n"
        "   outb %%al, $0x64\n"
        ::: "eax", "memory"
    );
    for(;;) __asm__ __volatile__("hlt");
}

=======
    extern uint64_t timer_ticks(void);
    char buf[64];
    uint64_t ticks = timer_ticks();
    snprintf(buf, sizeof(buf), "Uptime: %llu ticks (%llu ms)\n",
             ticks, ticks);
    shell_print(buf);
}

>>>>>>> 86b48d9e005102ecf781f5f192fd54d487851616
/* ---- Main shell loop ---- */

void shell_run(void)
{
    shell_print("\n");
    vga_set_color(VGA_COLOR_LIGHT_GREEN, VGA_COLOR_BLACK);
    shell_print("DevOS Shell — type 'help' for commands\n");
    vga_set_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK);

    char *argv[SHELL_ARGS_MAX];

    for (;;) {
        shell_readline();

        if (line_len == 0) continue;

        int argc = shell_parse(argv, SHELL_ARGS_MAX);
        if (argc == 0) continue;

        const char *cmd = argv[0];

        if      (strcmp(cmd, "help")   == 0) cmd_help();
        else if (strcmp(cmd, "clear")  == 0) cmd_clear();
        else if (strcmp(cmd, "meminfo")== 0) cmd_meminfo();
        else if (strcmp(cmd, "heapinfo")==0) cmd_heapinfo();
        else if (strcmp(cmd, "ps")     == 0) cmd_ps();
        else if (strcmp(cmd, "ls")     == 0) cmd_ls(argc > 1 ? argv[1] : NULL);
        else if (strcmp(cmd, "cat")    == 0) cmd_cat(argc > 1 ? argv[1] : NULL);
        else if (strcmp(cmd, "echo")   == 0) cmd_echo(argv, argc);
        else if (strcmp(cmd, "uptime") == 0) cmd_uptime();
<<<<<<< HEAD
        else if (strcmp(cmd, "halt")   == 0) cmd_halt();
        else if (strcmp(cmd, "reboot") == 0) cmd_reboot();
=======
>>>>>>> 86b48d9e005102ecf781f5f192fd54d487851616
        else {
            shell_print(cmd);
            shell_print(": command not found\n");
        }
    }
}
