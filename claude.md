# VibeOS - Claude Context

## Project Overview
VibeOS is a hobby operating system built from scratch for aarch64 (ARM64), targeting QEMU's virt machine. This is a science experiment to see what an LLM can build.

## The Vibe
- **Aesthetic**: Retro Mac System 7 / Apple Lisa (1-bit black & white, Chicago-style fonts, classic Mac UI)
- **Philosophy**: Simple, educational, nostalgic
- **NOT trying to be**: Linux, production-ready, or modern

## Team
- **Human**: Vibes only. Yells "fuck yeah" when things work. Cannot provide technical guidance.
- **Claude**: Full technical lead. Makes all architecture decisions. Wozniak energy.

## Current State (Last Updated: Session 1)
- [x] Bootloader (boot/boot.S) - Sets up stack, clears BSS, jumps to kernel
- [x] Minimal kernel (kernel/kernel.c) - UART output working
- [x] Linker script (linker.ld) - Memory layout for QEMU virt
- [x] Makefile - Builds and runs in QEMU
- [x] Boots successfully! Prints to serial console.

## Architecture Decisions Made
1. **Target**: QEMU virt machine, aarch64, Cortex-A72
2. **Memory start**: 0x40000000 (QEMU virt default)
3. **UART**: PL011 at 0x09000000 (QEMU virt default)
4. **Stack**: 64KB, placed after BSS
5. **Toolchain**: aarch64-elf-gcc (brew install)

## Roadmap (Terminal-First)
Phase 1: Kernel Foundations
1. Memory management - physical page allocator, heap (malloc/free)
2. libc basics - string functions, sprintf
3. Interrupts - GIC (interrupt controller), timer, keyboard
4. Processes - task struct, context switching, scheduler

Phase 2: Userspace
5. Syscall interface - svc instruction, syscall table
6. Init process - first userspace program
7. Shell - command parsing, job control
8. Filesystem - in-memory VFS, then simple disk format

Phase 3: Apps
9. Coreutils - ls, cat, cp, mv, rm, mkdir, pwd, echo, clear
10. Text editor - minimal vi/nano style
11. Snake - terminal-based game

Phase 4: GUI (Future)
12. Framebuffer driver
13. Window manager
14. Desktop/Finder

## Technical Notes

### QEMU virt Machine Memory Map
- 0x00000000 - 0x3FFFFFFF: Flash, peripherals
- 0x08000000: GIC (interrupt controller)
- 0x09000000: UART (PL011)
- 0x0A000000: RTC
- 0x0C000000: Virtio devices
- 0x40000000+: RAM (we load here)

### Key Files
- boot/boot.S - Entry point, CPU init
- kernel/kernel.c - Main kernel code
- linker.ld - Memory layout
- Makefile - Build system

### Build & Run
```bash
make        # Build
make run    # Run with GUI window (serial still in terminal)
make run-nographic  # Terminal only
```

## Architecture Decisions (Locked In)
| Component | Decision | Notes |
|-----------|----------|-------|
| Kernel | Monolithic | Everything in kernel space, Linux-style |
| Scheduler | Cooperative | Processes yield voluntarily, retro-authentic |
| Memory | Flat (no MMU) | No virtual memory, shared address space |
| Filesystem | Custom hierarchical | Case-insensitive, proper folder structure |
| Shell | POSIX-ish | Familiar syntax, pipes/redirects |
| Process model | fork/exec | Unix-style |
| RAM | 256MB | Configurable, max 8GB |
| Coreutils | ls cd pwd cat echo mkdir rm cp mv clear touch | The essentials |

## Session Log
### Session 1
- Created project structure
- Wrote bootloader, minimal kernel, linker script, Makefile
- Successfully booted in QEMU, UART output works
- Decided on retro Mac aesthetic
- Human confirmed: terminal-first approach, take it slow
