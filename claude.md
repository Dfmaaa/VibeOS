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

## Current State (Last Updated: Session 6)
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
- [x] VFS (kernel/vfs.c) - Now backed by FAT32, falls back to in-memory
- [x] Coreutils - ls, cd, pwd, mkdir, touch, cat, echo (with > redirect)
- [x] ELF loader (kernel/elf.c) - Can load/run ELF binaries
- [x] Process exec (kernel/process.c) - Win3.1 style, programs call kernel directly via kapi
- [x] Kernel API (kernel/kapi.c) - Function pointers for programs to call kernel
- [x] Text editor (kernel/vi.c) - Modal vi clone with normal/insert/command modes
- [x] Virtio block device (kernel/virtio_blk.c) - Read/write disk sectors
- [x] FAT32 filesystem (kernel/fat32.c) - Read-only, supports long filenames
- [x] Persistent storage - 64MB FAT32 disk image, mountable on macOS
- [ ] Interrupts - GIC/timer code exists but disabled (breaks virtio - unknown bug)

## Architecture Decisions Made
1. **Target**: QEMU virt machine, aarch64, Cortex-A72
2. **Memory start**: 0x40000000 (QEMU virt default)
3. **UART**: PL011 at 0x09000000 (QEMU virt default)
4. **Stack**: 64KB, placed in .stack section after BSS
5. **Toolchain**: aarch64-elf-gcc (brew install)
6. **Compiler flags**: -mgeneral-regs-only (no SIMD)
7. **Process model**: Win3.1 style - no memory protection, programs run in kernel space

## Roadmap (Terminal-First)
Phase 1: Kernel Foundations - DONE
1. ~~Memory management~~ - heap allocator working
2. ~~libc basics~~ - string functions, sprintf
3. ~~Display~~ - framebuffer, console, font
4. ~~Keyboard~~ - virtio-input with shift keys
5. ~~Shell~~ - in-kernel with basic commands
6. ~~Filesystem~~ - in-memory VFS

Phase 2: Programs - MONOLITH APPROACH
7. ~~ELF loader~~ - working but abandoned
8. ~~Kernel API~~ - kapi struct with function pointers
9. **DECISION**: Monolith kernel - all commands built into shell
   - Tried external programs, hit linker issues with 6+ embedded binaries
   - Win3.1 vibes - everything in one binary is fine

Phase 3: Apps (IN PROGRESS)
10. ~~Text editor~~ - vi clone with modal editing (normal/insert/command modes)
11. Snake - terminal-based game (NEXT)
12. More features as needed

Phase 4: GUI (Future)
13. Window manager
14. Desktop/Finder
15. DOOM?

## Technical Notes

### QEMU virt Machine Memory Map
- 0x00000000 - 0x3FFFFFFF: Flash, peripherals
- 0x08000000: GIC (interrupt controller)
- 0x09000000: UART (PL011)
- 0x0A000000: RTC
- 0x0A003E00: Virtio keyboard (device 31)
- 0x40000000+: RAM (we load here)
- 0x40200000: Program load address (if using external programs)

### Key Files
- boot/boot.S - Entry point, CPU init, BSS clear
- kernel/kernel.c - Main kernel code
- kernel/memory.c/.h - Heap allocator (malloc/free)
- kernel/string.c/.h - String/memory functions
- kernel/printf.c/.h - Printf implementation
- kernel/fb.c/.h - Framebuffer driver (ramfb)
- kernel/console.c/.h - Text console (with UART fallback)
- kernel/font.c/.h - Bitmap font
- kernel/keyboard.c/.h - Virtio keyboard driver
- kernel/virtio_blk.c/.h - Virtio block device driver
- kernel/fat32.c/.h - FAT32 filesystem driver (read-only)
- kernel/shell.c/.h - In-kernel shell with all commands
- kernel/vfs.c/.h - Virtual filesystem (backed by FAT32 or in-memory)
- kernel/vi.c/.h - Modal text editor (vi clone)
- kernel/elf.c/.h - ELF64 loader
- kernel/process.c/.h - Process execution
- kernel/kapi.c/.h - Kernel API for programs
- kernel/initramfs.c/.h - Binary embedding (currently unused)
- linker.ld - Memory layout
- Makefile - Build system
- disk.img - FAT32 disk image (created by `make disk`)

### User Directory (currently unused)
- user/lib/vibe.h - Userspace library header
- user/lib/crt0.S - C runtime startup
- user/bin/*.c - Program sources
- user/linker.ld - Program linker script

### Build & Run
```bash
make        # Build kernel
make disk   # Create FAT32 disk image (only needed once)
make run    # Run with GUI window (serial still in terminal)
make run-nographic  # Terminal only
make distclean  # Clean everything including disk image
```

### Mounting the Disk Image (macOS)
```bash
hdiutil attach disk.img        # Mount
# ... add/edit files in /Volumes/VIBEOS/ ...
hdiutil detach /Volumes/VIBEOS # Unmount before running QEMU
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
| vi <file> | Edit file (modal editor) |

### VFS Structure
```
/
├── bin/        (empty - monolith kernel)
├── etc/
│   └── motd    (message of the day)
├── home/
│   └── user/   (default cwd)
└── tmp/
```

## Architecture Decisions (Locked In)
| Component | Decision | Notes |
|-----------|----------|-------|
| Kernel | Monolithic | Everything in kernel space, Win3.1-style |
| Programs | Built-in | All commands in shell, no external binaries |
| Memory | Flat (no MMU) | No virtual memory, shared address space |
| Filesystem | FAT32 on virtio-blk | Persistent, mountable on host, read-only for now |
| Shell | POSIX-ish | Familiar syntax, basic redirects |
| RAM | 256MB | Configurable |
| Disk | 64MB FAT32 | Persistent storage via virtio-blk |
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
- **Stack in BSS**: Boot hangs if stack is in .bss section - it gets zeroed while in use! Put stack in separate .stack section.
- **Embedded binaries**: objcopy binary embedding breaks with 6+ programs. Linker issue. Just use monolith kernel instead.
- **Console without framebuffer**: console_puts/putc fall back to UART if fb_base is NULL.
- **kapi colors**: Must use uint32_t for colors (RGB values like 0x00FF00), not uint8_t.
- **Packed structs on ARM**: Accessing fields in `__attribute__((packed))` structs causes unaligned access faults. Read bytes individually and assemble values manually.
- **FAT32 minimum size**: FAT32 requires at least ~33MB. Use 64MB disk image.
- **Virtio-blk polling**: Save `used->idx` before submitting request, then poll until it changes. Don't use a global `last_used_idx` that persists across requests.

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

### Session 3
- Attempted proper userspace with syscalls (SVC instruction)
- SVC never triggered exception handler - extensive debugging failed
- Pivoted to Win3.1 style: programs run in kernel space, call kapi directly
- Built ELF loader and process execution
- Created kapi (kernel API) - struct of function pointers
- Fixed multiple bugs: ELF load address (0x40200000), crt0 return address, color types
- Attempted to move shell commands to /bin as separate programs
- Hit weird linker bug: 5 embedded programs work, 6 breaks boot
- Extensive debugging: not size, not specific program, just "6th binary breaks it"
- Stack was in BSS and getting zeroed - fixed by putting in .stack section
- Still couldn't fix the 6-binary limit
- **DECISION**: Monolith kernel. All commands stay in shell. Fuck it, it's VibeOS.
- Final kernel: 28KB, all features working

### Session 6
- Revisited the external binaries problem - decided to use persistent FAT32 filesystem instead
- Built virtio-blk driver for block device access
- Implemented FAT32 filesystem driver (read-only)
- Integrated FAT32 with VFS - now `/` is backed by the disk image
- Updated Makefile to create and format 64MB FAT32 disk image
- Fixed multiple bugs:
  - Virtio-blk polling logic (was using stale `last_used_idx`)
  - Packed struct access on ARM (unaligned access faults) - read bytes manually
  - FAT32 minimum size requirement (increased from 32MB to 64MB)
- Disk image is mountable on macOS with `hdiutil attach disk.img`
- Can now put binaries on disk and load them at runtime (solves the 6-binary limit!)
- **Achievement**: Persistent filesystem working! Files survive reboots!
