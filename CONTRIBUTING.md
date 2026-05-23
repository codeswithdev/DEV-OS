# Contributing to DevOS

DevOS is India's first from-scratch x86_64 OS. We welcome contributors who want to get their hands dirty with real kernel engineering.

## Current Status (v0.4)

| Component | Status |
|---|---|
| Bootloader (BIOS MBR + Stage 2) | ✅ Done |
| Kernel Init (GDT, IDT, PIC, Serial) | ✅ Done |
| Memory Manager (PMM, VMM, Heap) | ✅ Done |
| Scheduler (Round-Robin + sleep) | ✅ Done |
| VFS + RamFS | ✅ Done |
| Syscall interface (13 syscalls) | ✅ Done |
| ELF Loader | ✅ Done |
| Kernel Shell | ✅ Done |
| Drivers (VGA, PS/2 Keyboard) | ✅ Done |
| Userland programs | 🔧 Minimal |
| Disk driver (ATA/AHCI) | ⏳ Next |
| Network stack | ⏳ Planned |

## How to Build

### Requirements
- `nasm` — assembler
- `x86_64-linux-gnu-gcc` — cross compiler
- `x86_64-linux-gnu-ld` — linker
- `qemu-system-x86_64` — emulator
- `gdb` — debugger (optional)

On Ubuntu/Debian:
```bash
sudo apt install nasm gcc-x86-64-linux-gnu binutils-x86-64-linux-gnu qemu-system-x86
```

### Build & Run
```bash
git clone https://github.com/codeswithdev/dev-os.git
cd dev-os
make
make run
```

### Debug with GDB
```bash
make debug
# In another terminal:
gdb build/kernel.elf -ex "target remote :1234"
```

## Good First Issues

- Add more shell commands (`mkdir`, `rm`, `touch`, `write`)
- Improve VGA driver (scrolling, cursor positioning)
- Add more syscalls (`fork`, `exec`, `waitpid`)
- Write a simple ATA PIO disk driver
- Port a minimal libc (`printf`, `malloc`, `free` wrappers)

## Code Style

- C11 standard, freestanding (no libc)
- `snake_case` for functions and variables
- 4-space indentation
- Every file starts with a brief comment describing its purpose
- No external dependencies — if it doesn't exist in `kernel/lib/`, write it yourself

## Areas Open for Contribution

- `kernel/drivers/` — PCI enumeration, AHCI, PS/2 mouse, serial improvements
- `kernel/fs/` — ext2 on top of VFS, a simple FAT12/16 reader
- `userland/` — more userspace programs, a mini libc
- `docs/` — architecture documentation, build guides

## Submitting Changes

1. Fork the repo
2. Create a branch: `git checkout -b feat/your-feature`
3. Commit with clear messages: `[kernel] add PCI device enumeration`
4. Open a pull request describing what changed and why

## Questions?

Open an issue or start a discussion on GitHub. Low-level OS development is hard — asking questions is encouraged.
