# DEV-OS
🚀 DevOS is an ambitious open-source initiative to build India’s first fully independent operating system from absolute scratch — including a custom bootloader, kernel, memory management, scheduler, drivers, filesystem, and future AI-native architecture. No wrappers. Built from bare metal.


# DevOS

🚀 An independent, from-scratch x86_64 operating system.

No Linux. No BSD. No wrappers.  
Just bare metal, Intel manuals, and low-level engineering.

---

## Status

| Component | Status |
|---|---|
| Bootloader | 🔧 In Progress |
| Kernel Init | ⏳ Planned |
| Memory Manager | ⏳ Planned |
| Scheduler | ⏳ Planned |
| Drivers | ⏳ Planned |

---

## What is DevOS?

DevOS is a fully independent operating system project focused on building:

- Custom x86_64 bootloader
- Freestanding C kernel
- Physical + Virtual memory manager
- Interrupt handling system
- Process scheduler
- Filesystem
- Drivers
- Shell + userspace

This is not a Linux distribution or tutorial clone.

This is real systems engineering.

---

## Quick Start

```bash
git clone https://github.com/codeswithdev/dev-os.git
cd dev-os
make
make run
```

---

## Current Goal

Current milestone:

```text
Print "DevOS Booted"
directly from the bootloader in QEMU.
```

---

## Tech Stack

- NASM Assembly
- Freestanding C
- x86_64 Architecture
- QEMU
- GCC Cross Compiler
- GDB

---

## Repository Structure

```text
boot/
kernel/
drivers/
mm/
fs/
docs/
```

---
Roadmap
Phase Status
Description
1
🔧
 In Progress Bootloader (BIOS MBR + Stage 2)
2
⏳
 Planned
Kernel Init (GDT, IDT, VGA, Serial)
3
⏳
 Planned
Memory Management (PMM, VMM, Heap)
4
⏳
 Planned
Scheduler + Multitasking
5
⏳
 Planned
Drivers (PCI, PS/2, AHCI)
6
⏳
 Planned
Filesystem (VFS, Ext2)
7
⏳
 Planned
Shell + Userspace


## Contributing

DevOS is currently in early-stage kernel development.

Contributors interested in:
- low-level systems
- assembly
- compilers
- kernel engineering

Are welcome.