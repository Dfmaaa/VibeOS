# VibeOS - Claude Context

**IMPORTANT: THERE IS NEVER A READ-ONLY DISK ERROR. `make clean && make` WILL PREPARE THE DISK CORRECTLY AND IT WILL WORK. DO NOT USE HDIUTIL TO CHECK OR DEBUG DISK ISSUES.**

## Project Overview
VibeOS is a hobby operating system built from scratch for aarch64 (ARM64), targeting QEMU's virt machine. This is a science experiment to see what an LLM can build.

## The Vibe
- **Aesthetic**: Retro Mac System 7 / Apple Lisa (1-bit black & white, Chicago-style fonts, classic Mac UI)
- **Philosophy**: Simple, educational, nostalgic
- **NOT trying to be**: Linux, production-ready, or modern

## Team
- **Human**: Vibes only. Yells "fuck yeah" when things work. Cannot provide technical guidance.
- **Claude**: Full technical lead. Makes all architecture decisions. Wozniak energy.

## Current State (Last Updated: Session 16)
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
- [x] Coreutils - ls, cd, pwd, mkdir, touch, rm, cat, echo (with > redirect)
- [x] ELF loader (kernel/elf.c) - Loads PIE binaries with full relocation support
- [x] Process management (kernel/process.c) - Process table, context switching, scheduler
- [x] Cooperative multitasking - yield(), spawn() in kapi, round-robin scheduler
- [x] Kernel API (kernel/kapi.c) - Function pointers for programs to call kernel
- [x] Text editor (kernel/vi.c) - Modal vi clone with normal/insert/command modes
- [x] Virtio block device (kernel/virtio_blk.c) - Read/write disk sectors
- [x] FAT32 filesystem (kernel/fat32.c) - Read/write, supports long filenames
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

Phase 3: Apps (DONE)
10. ~~Text editor~~ - vi clone with modal editing (normal/insert/command modes)
11. ~~Snake~~ - moved to /bin/snake userspace program
12. ~~Tetris~~ - moved to /bin/tetris userspace program

Phase 4: GUI (IN PROGRESS)
13. ~~Mouse driver~~ - virtio-tablet support
14. ~~Window manager~~ - /bin/desktop with draggable windows, close boxes
15. ~~Double buffering~~ - reduces flicker
16. ~~Terminal emulator~~ - /bin/term with stdio hooks
17. ~~Visual refresh~~ - True 1-bit B&W System 7 aesthetic
18. Notepad/text editor, file explorer - TODO
19. DOOM?

## Technical Notes

### QEMU virt Machine Memory Map
- 0x00000000 - 0x3FFFFFFF: Flash, peripherals
- 0x08000000: GIC (interrupt controller)
- 0x09000000: UART (PL011)
- 0x0A000000: RTC
- 0x0A003E00: Virtio keyboard (device 31)
- 0x40000000+: RAM (we load here)
- 0x41000000+: Program load area (dynamically allocated by kernel)

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
- kernel/fat32.c/.h - FAT32 filesystem driver (read/write)
- kernel/shell.c/.h - In-kernel shell with all commands
- kernel/vfs.c/.h - Virtual filesystem (backed by FAT32 or in-memory)
- kernel/vi.c/.h - Modal text editor (vi clone)
- kernel/elf.c/.h - ELF64 loader (supports PIE binaries)
- kernel/process.c/.h - Process table, scheduler, context switching
- kernel/context.S - Assembly context switch routine
- kernel/kapi.c/.h - Kernel API for programs
- kernel/initramfs.c/.h - Binary embedding (currently unused)
- linker.ld - Memory layout
- Makefile - Build system
- disk.img - FAT32 disk image (created by `make disk`)

### User Directory (userspace programs)
- user/lib/vibe.h - Userspace library header
- user/lib/crt0.S - C runtime startup
- user/bin/*.c - Program sources
- user/linker.ld - Program linker script (PIE, base at 0x0)

### Build & Run
```bash
make            # Build kernel AND all user programs (everything)
make clean      # Clean build artifacts
make disk       # Create FAT32 disk image (only needed once)
make run        # Run with GUI window (serial still in terminal)
make run-nographic  # Terminal only
make distclean  # Clean everything including disk image
```

**IMPORTANT**: `make` builds EVERYTHING - kernel and all user programs. There is no separate user-progs target. Just use `make clean && make` to rebuild.

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
| rm <file> | Remove file |
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
| Programs | PIE on disk | Loaded dynamically at runtime, kernel picks address |
| Memory | Flat (no MMU) | No virtual memory, shared address space |
| Multitasking | Cooperative | Programs call yield(), round-robin scheduler |
| Filesystem | FAT32 on virtio-blk | Persistent, mountable on host, read/write |
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
- **Virtio-input device detection**: Both keyboard and tablet are virtio-input. Must check device name contains "Keyboard" specifically, not just starts with "Q".
- **Userspace has no stdint.h**: Use `unsigned long` instead of `uint64_t` in user programs, or define types in vibe.h.
- **PIE on AArch64**: Use `-fPIE` and `-pie` flags. ELF loader processes R_AARCH64_RELATIVE relocations at load time.
- **Context switch**: Only need to save callee-saved registers (x19-x30, sp). Caller-saved regs are already on stack.
- **PIE relocations (FIXED!)**: Use `-O0` for userspace to ensure GCC generates relocations for static pointer initializers. With `-O2`, GCC tries to be clever and compute addresses at runtime, but puts structs in BSS (zeroed) so pointers are NULL. The ELF loader now processes `.rela.dyn` section and fixes up `R_AARCH64_RELATIVE` entries. Normal C code with pointers now works!

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

### Session 7
- Made FAT32 filesystem writable!
- Added FAT table write support (fat_set_cluster) - updates both FAT copies
- Added cluster allocation (fat_alloc_cluster) - finds free clusters
- Added cluster chain freeing (fat_free_chain)
- Implemented fat32_create_file() - create empty files
- Implemented fat32_mkdir() - create directories with . and .. entries
- Implemented fat32_write_file() - write data to files, handles cluster allocation
- Implemented fat32_delete() - delete files and free their clusters
- Updated VFS layer to use FAT32 write functions
- Now mkdir, touch, echo > file all persist to disk!
- Files created in VibeOS are visible when mounting disk.img on macOS
- **Achievement**: Full read/write persistent filesystem!

### Session 8
- Moved snake and tetris from kernel to userspace (/bin/snake, /bin/tetris)
- Extended kapi with framebuffer access (fb_base, width, height, drawing functions)
- Extended kapi with mouse input (position, buttons, poll)
- Added uart_puts to kapi for direct UART debug output
- Built virtio-tablet mouse driver (kernel/mouse.c)
- Fixed keyboard detection to not conflict with tablet (both are virtio-input)
- Created /bin/desktop - window manager with:
  - Classic Mac System 7 aesthetic (gray desktop, striped title bars)
  - Draggable windows by title bar
  - Close boxes that work
  - Menu bar (File, Edit, View, Special)
  - Mouse cursor with save/restore
  - Double buffering to reduce flicker
- Fixed heap/program memory overlap bug:
  - Backbuffer allocation (~2MB) was overlapping program load address
  - Moved program load address from 0x40200000 to 0x40400000
- **Achievement**: GUI desktop environment working!

### Session 9
- Implemented dynamic program loading - no more hardcoded addresses!
- Converted userspace programs to PIE (Position Independent Executables)
  - Updated user/linker.ld to base at 0x0
  - Added `-fPIE` and `-pie` compiler/linker flags
- Enhanced ELF loader:
  - elf_load_at() loads PIE binaries at any address
  - elf_calc_size() calculates memory requirements
  - Supports both ET_EXEC and ET_DYN types
- Built cooperative multitasking infrastructure:
  - Process table (MAX_PROCESSES = 16)
  - Process states: FREE, READY, RUNNING, BLOCKED, ZOMBIE
  - Round-robin scheduler
  - Context switching in assembly (kernel/context.S)
  - yield() and spawn() added to kapi
- Programs now load at 0x41000000+ with kernel picking addresses dynamically
- Tested: desktop→snake→tetris→desktop all load at different addresses
- **Achievement**: Dynamic loading and multitasking foundation complete!

### Session 10
- Added Apple menu with dropdown (About VibeOS..., Quit Desktop)
- Removed Q keyboard shortcut - quit only via Apple menu now
- Added font_data pointer to kapi for userspace text rendering
- Fixed window rendering - all drawing now goes to backbuffer:
  - Added bb_draw_char/bb_draw_string for backbuffer text
  - Windows properly occlude each other (no text bleed-through)
  - Title bars, content areas, and borders all render correctly
- Created LONGTERM.md with roadmap

### Session 11
- Added dock bar at bottom of screen with app icons
- Built Calculator app (first desktop app!):
  - Calculator icon in dock (32x32 pixel art)
  - Click dock icon to open calculator window
  - Working integer arithmetic (+, -, *, /)
  - 3D button styling with proper hit detection
  - Shows pending operation in display
  - Fixed PIE string literal issue (2D char* array → flat char[][] array)
- Improved menu bar layout and spacing
- Windows can't be dragged below the dock

### Session 12
- Built File Explorer app:
  - Folder icon in dock
  - Navigate directories (click to select, double-click to enter)
  - Path bar showing current location
  - ".." to go up a directory
  - Right-click context menu with: New File, New Folder, Rename, Delete
  - Inline rename with keyboard input (Enter to confirm, Escape to cancel)
- Added rm command to shell
- Added vfs_delete() and vfs_rename() to VFS layer
- Added fat32_rename() - modifies directory entry name in place
- Extended kapi with delete, rename, readdir, set_cwd, get_cwd
- Fixed macOS junk files (._, .DS_Store, .fseventsd) in Makefile:
  - Disk mounts with -nobrowse to prevent Finder indexing
  - Cleanup commands run after every mount/install

### Session 13
- Added snake and tetris game launchers to dock:
  - Snake icon (32x32 pixel art green snake)
  - Tetris icon (32x32 colorful blocks)
  - Click to launch games from desktop
- Fixed cooperative multitasking to actually work:
  - Added yield() calls to desktop, snake, and tetris main loops
  - Fixed process_yield() to work from kernel context (was returning early)
  - Made process_exec() create real process entries (was direct function call)
  - Added kernel_context to save/restore when switching between kernel and processes
- Games now use exec() instead of spawn():
  - Desktop blocks while game runs, resumes when game exits
  - Clean handoff - no screen fighting between processes
- **Achievement**: Can launch games from dock and return to desktop!

### Session 14
- Built userspace shell `/bin/vibesh`:
  - Boots directly into vibesh (kernel shell is now just a bootstrap)
  - Parses commands, handles builtins (cd, exit, help)
  - Executes external programs from /bin with argument passing
- Built userspace coreutils:
  - `/bin/echo` - with output redirection support (echo foo > file)
  - `/bin/ls` - uses proper readdir API
  - `/bin/cat` - supports multiple files
  - `/bin/pwd` - print working directory
  - `/bin/mkdir`, `/bin/touch`, `/bin/rm` - file operations
- Added `exec_args` to kapi for passing argc/argv to programs
- Added `console_rows`/`console_cols` to kapi
- Fixed process exit bug:
  - process_exit() was returning when no other processes existed
  - Now context switches directly back to kernel_context
- Attempted userspace vi but hit PIE/BSS issues:
  - Static variables in PIE binaries don't work reliably
  - Even heap-allocated state with function pointer calls hangs
  - Abandoned - will make GUI editor instead
- **Achievement**: Userspace shell and coreutils working!

### Session 15
- **MAJOR BREAKTHROUGH: Fixed PIE relocations!**
  - Userspace C code now works like normal C - pointers, string literals, everything
  - Problem: Static initializers with pointers (e.g., `const char *label = "hello"`) were broken
  - Root cause: With `-O2`, GCC generates PC-relative code but puts struct in BSS (zeroed), so pointers are NULL
  - Solution 1: Use `-O0` for userspace so GCC generates proper relocations
  - Solution 2: ELF loader now processes `.rela.dyn` section with `R_AARCH64_RELATIVE` entries
  - Added `Elf64_Dyn`, `Elf64_Rela` structs and `elf_process_relocations()` to kernel/elf.c
  - Formula: `*(load_base + offset) = load_base + addend`
- Rebuilt desktop from scratch (old one was a mess with everything embedded):
  - Clean window manager architecture
  - Desktop just manages windows, dock, menu bar, cursor
  - Apps are separate binaries that use window API via kapi
  - Fullscreen apps (snake, tetris) use exec() - desktop waits
  - Windowed apps (calc) use spawn() + window API
- Built Calculator app (`/bin/calc`):
  - First proper windowed app using the new window API
  - Creates window, gets buffer, draws buttons, receives events
  - Working integer arithmetic
- Window API in kapi:
  - `window_create()`, `window_destroy()`, `window_get_buffer()`
  - `window_poll_event()`, `window_invalidate()`, `window_set_title()`
  - Desktop registers these functions at startup
- **Achievement**: Can now write normal C code in userspace! This unlocks everything.

### Session 16
- **Visual refresh - True Mac System 7 aesthetic!**
  - Pure 1-bit black & white color scheme
  - Classic Mac diagonal checkerboard desktop pattern
  - Apple logo (16x16 bitmap) in menu bar
  - Beautiful 32x32 pixel art dock icons: Snake, Tetris, Calculator, Files, Terminal
  - System 7 window chrome: horizontal stripes on focused title bars, drop shadows, double-line borders
  - Close box with inner square when focused
  - Clock in menu bar (decorative)
- **Built Terminal Emulator (`/bin/term`)!**
  - 80x24 character window with monospace font
  - Spawns vibesh shell inside the window
  - Stdio hooks mechanism: `stdio_putc`, `stdio_puts`, `stdio_getc`, `stdio_has_key`
  - Shell and all coreutils use hooks when available, fall back to console otherwise
  - Keyboard input via window events → ring buffer → shell reads
  - Inverse block cursor
- **Updated all coreutils for terminal support:**
  - ls, cat, echo, pwd, mkdir, touch, rm all use `out_puts`/`out_putc` helpers
  - Check for stdio hooks, use them if set, otherwise use console I/O
  - Works in both kernel console AND terminal window
- **Achievement**: Shell running inside a GUI window! Can run commands, see output, everything works!

**NEXT SESSION TODO:**
- Build notepad/GUI text editor
- Build file explorer as windowed app
- Maybe DOOM?
