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

## Current State (Last Updated: Session 2)
- [x] Bootloader (boot/boot.S) - Sets up stack, clears BSS, jumps to kernel
- [x] Minimal kernel (kernel/kernel.c) - UART output working
- [x] Linker script (linker.ld) - Memory layout for QEMU virt
- [x] Makefile - Builds and runs in QEMU
- [x] Boots successfully! Prints to serial console.
- [x] Memory management (kernel/memory.c) - malloc/free working, 255MB heap
- [x] String functions (kernel/string.c) - memcpy, strlen, strcmp, strtok_r, etc.
- [x] Printf (kernel/printf.c) - %d, %s, %x, %p working
- [x] Framebuffer (kernel/fb.c) - ramfb device, 800x600
- [x] Bitmap font (kernel/font.c) - 8x16 VGA-style font
- [x] Console (kernel/console.c) - Text console with colors on screen
- [x] Virtio keyboard (kernel/keyboard.c) - Full keyboard with shift support
- [x] Shell (kernel/shell.c) - In-kernel shell with commands
- [x] VFS (kernel/vfs.c) - In-memory filesystem with directories and files
- [x] Coreutils - ls, cd, pwd, mkdir, touch, cat, echo (with > redirect)
- [ ] Interrupts - GIC/timer code exists but disabled (breaks virtio - unknown bug)
- [ ] Syscalls - Move shell to userspace
- [ ] Processes - task struct, context switching, scheduler

## Architecture Decisions Made
1. **Target**: QEMU virt machine, aarch64, Cortex-A72
2. **Memory start**: 0x40000000 (QEMU virt default)
3. **UART**: PL011 at 0x09000000 (QEMU virt default)
4. **Stack**: 64KB, placed after BSS
5. **Toolchain**: aarch64-elf-gcc (brew install)
6. **Compiler flags**: -mgeneral-regs-only (no SIMD)

## Roadmap (Terminal-First)
Phase 1: Kernel Foundations - DONE
1. ~~Memory management~~ - heap allocator working
2. ~~libc basics~~ - string functions, sprintf
3. ~~Display~~ - framebuffer, console, font
4. ~~Keyboard~~ - virtio-input with shift keys
5. ~~Shell~~ - in-kernel with basic commands
6. ~~Filesystem~~ - in-memory VFS

Phase 2: Userspace (NEXT)
7. Syscall interface - svc instruction, syscall table
8. Init process - first userspace program
9. Move shell to userspace
10. Processes - task struct, context switching

Phase 3: Apps
11. More coreutils - cp, mv, rm
12. Text editor - minimal vi/nano style
13. Snake - terminal-based game

Phase 4: GUI (Future)
14. Window manager
15. Desktop/Finder

## Technical Notes

### QEMU virt Machine Memory Map
- 0x00000000 - 0x3FFFFFFF: Flash, peripherals
- 0x08000000: GIC (interrupt controller)
- 0x09000000: UART (PL011)
- 0x0A000000: RTC
- 0x0A003E00: Virtio keyboard (device 31)
- 0x40000000+: RAM (we load here)

### Key Files
- boot/boot.S - Entry point, CPU init
- kernel/kernel.c - Main kernel code
- kernel/memory.c/.h - Heap allocator (malloc/free)
- kernel/string.c/.h - String/memory functions
- kernel/printf.c/.h - Printf implementation
- kernel/fb.c/.h - Framebuffer driver (ramfb)
- kernel/console.c/.h - Text console
- kernel/font.c/.h - Bitmap font
- kernel/keyboard.c/.h - Virtio keyboard driver
- kernel/shell.c/.h - In-kernel shell
- kernel/vfs.c/.h - In-memory filesystem
- kernel/gic.c/.h - GIC interrupt controller (unused)
- kernel/timer.c/.h - ARM timer (unused)
- kernel/irq.c/.h - IRQ handling (unused)
- kernel/exceptions.S - Exception vectors (unused)
- linker.ld - Memory layout
- Makefile - Build system

### Build & Run
```bash
make        # Build
make run    # Run with GUI window (serial still in terminal)
make run-nographic  # Terminal only
```

### Shell Commands
| Command | Description |
|---------|-------------|
| help | Show available commands |
| clear | Clear screen |
| echo [text] | Print text (supports > redirect) |
| version | Show VibeOS version |
| mem | Show memory info |
| pwd | Print working directory |
| ls [path] | List directory contents |
| cd <path> | Change directory |
| mkdir <dir> | Create directory |
| touch <file> | Create empty file |
| cat <file> | Show file contents |

### VFS Structure
```
/
├── bin/        (empty for now)
├── home/
│   └── user/   (default cwd)
└── tmp/
```

## Architecture Decisions (Locked In)
| Component | Decision | Notes |
|-----------|----------|-------|
| Kernel | Monolithic | Everything in kernel space, Linux-style |
| Scheduler | Cooperative | Processes yield voluntarily, retro-authentic |
| Memory | Flat (no MMU) | No virtual memory, shared address space |
| Filesystem | Custom hierarchical | In-memory VFS for now |
| Shell | POSIX-ish | Familiar syntax, basic redirects |
| Process model | fork/exec | Unix-style (future) |
| RAM | 256MB | Configurable |
| Interrupts | Disabled | Polling works, interrupts break virtio (bug) |

## Gotchas / Lessons Learned
- **aarch64 va_list**: Can't pass va_list to helper functions easily. Inline the va_arg handling.
- **QEMU virt machine**: Uses PL011 UART at 0x09000000, GIC at 0x08000000
- **Virtio legacy vs modern**: QEMU defaults to legacy virtio (version 1). Use `-global virtio-mmio.force-legacy=false` to get modern virtio (version 2).
- **Virtio memory barriers**: ARM needs `dsb sy` barriers around device register access.
- **strncpy hangs**: Our strncpy implementation causes hangs in some cases. Use manual loops instead.
- **Static array memset**: Don't memset large static arrays - they're already zero-initialized.
- **Interrupts + Virtio**: Enabling GIC interrupts breaks virtio keyboard polling. Unknown root cause. Skipped for now - cooperative multitasking doesn't need interrupts anyway.
- **-mgeneral-regs-only**: Use this flag to prevent GCC from using SIMD registers.

## Session Log
### Session 1
- Created project structure
- Wrote bootloader, minimal kernel, linker script, Makefile
- Successfully booted in QEMU, UART output works
- Decided on retro Mac aesthetic
- Human confirmed: terminal-first approach, take it slow
- Added memory management (heap allocator) - working
- Added string functions and printf - working after fixing va_list issue

### Session 2
- Fixed virtio keyboard (was using legacy mode, switched to modern with force-legacy=false)
- Built framebuffer driver using ramfb
- Added bitmap font and text console with colors
- Built in-kernel shell with commands
- Attempted interrupts (GIC, timer, exception vectors) - breaks virtio keyboard
- Debugged extensively, even asked Gemini - couldn't find root cause
- Decided to skip interrupts (cooperative multitasking doesn't need them)
- Built in-memory VFS filesystem
- Added coreutils: ls, cd, pwd, mkdir, touch, cat
- Added echo with > redirect for writing files
- Added shift key support for keyboard (uppercase, symbols like >)
- Everything working! Shell, filesystem, keyboard all functional
