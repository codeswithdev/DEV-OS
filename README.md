# DevOS

**India's first from-scratch x86_64 operating system.**  
No Linux. No BSD. No wrappers. Just bare metal, Intel manuals, and assembly.

---

## What is DevOS?

DevOS is a fully independent OS built from absolute zero — custom bootloader, kernel, memory manager, scheduler, filesystem, syscalls, and shell. Every line of code exists because someone wrote it, not because a framework provided it.

This is not a Linux distribution. This is not a tutorial clone.  
This is real systems engineering.

---

## Current Status — v0.4

| Component | Status | Details |
|---|---|---|
| Bootloader (BIOS MBR + Stage 2) | ✅ Working | x86 real mode → protected mode → long mode |
| Kernel Init | ✅ Working | GDT, IDT, PIC/IRQ, serial console |
| Physical Memory Manager | ✅ Working | E820 memory map, page allocator |
| Virtual Memory Manager | ✅ Working | x86_64 4-level paging, kernel address space |
| Kernel Heap | ✅ Working | Slab allocator (≤2KB) + large allocator |
| Process Scheduler | ✅ Working | Round-robin, preemptive, timer-driven |
| Sleep / Block | ✅ Working | `sched_block`, sleep list, wake on tick |
| VFS + RamFS | ✅ Working | Virtual filesystem, ramfs at `/`, `/dev` `/proc` `/tmp` |
| Syscall Interface | ✅ Working | 13 syscalls via `syscall` instruction (ring0 ↔ ring3) |
| ELF Loader | ✅ Working | Loads userspace ELF, creates ring3 process |
| Kernel Shell | ✅ Working | `help`, `ls`, `cat`, `ps`, `meminfo`, `uptime`, `reboot`, `halt` |
| Drivers | ✅ Working | VGA text mode, PS/2 keyboard |
| Userland | 🔧 Minimal | One test ELF; real userspace programs in progress |
| Disk Driver | ⏳ Next | ATA PIO planned |
| Network | ⏳ Planned | Far future |

---

## Quick Start

### Requirements

```bash
# Ubuntu / Debian
sudo apt install nasm gcc-x86-64-linux-gnu binutils-x86-64-linux-gnu qemu-system-x86
```

### Build & Run

```bash
git clone https://github.com/codeswithdev/dev-os.git
cd dev-os
make
make run
```

You should see the DevOS shell:
```
DevOS v0.4 — Booting...
[OK] Kernel online — DevOS v0.4
DevOS Shell — type 'help' for commands
devos>
```

### Debug with GDB

```bash
make debug
# GDB connects automatically to QEMU via :1234
```

---

## Architecture

```
boot/
  bios/
    stage1.asm        — 512-byte MBR bootloader
    stage2.asm        — loads kernel, enters long mode

kernel/
  arch/x86_64/        — GDT, IDT, IRQ, timer, syscall entry, context switch
  mm/                 — PMM (physical), VMM (virtual), heap (slab + large)
  sched/              — round-robin scheduler, sleep/wake, task management
  fs/                 — VFS layer + RamFS implementation
  drivers/            — VGA text mode, PS/2 keyboard
  elf/                — ELF64 loader for userspace programs
  syscall/            — syscall dispatch (read, write, open, mmap, exit, ...)
  proc/               — process table, waitpid
  shell/              — interactive kernel shell
  lib/                — printf, string, memset/memcpy

userland/
  test.asm            — minimal ring3 ELF test program
```

---

## Tech Stack

- NASM — bootloader and low-level stubs
- Freestanding C11 — kernel
- x86_64 — target architecture
- QEMU — emulation and testing
- GCC cross-compiler (`x86_64-linux-gnu-gcc`)
- GDB — kernel debugging

---

## Roadmap

| Phase | Status | Goal |
|---|---|---|
| 1 | ✅ | Bootloader (BIOS MBR + Stage 2) |
| 2 | ✅ | Kernel Init (GDT, IDT, VGA, Serial) |
| 3 | ✅ | Memory Management (PMM, VMM, Heap) |
| 4 | ✅ | Scheduler + Multitasking |
| 5 | ✅ | VFS, RamFS, Syscalls, ELF, Shell |
| 6 | 🔧 | Userland (ring3 shell, mini libc) |
| 7 | ⏳ | Disk Driver (ATA PIO → ext2) |
| 8 | ⏳ | PCI enumeration, more drivers |
| 9 | ⏳ | Network stack |

---

## Contributing

See [CONTRIBUTING.md](CONTRIBUTING.md) for build instructions, good first issues, and code style.

---

## License

MIT — see [LICENSE](LICENSE)
