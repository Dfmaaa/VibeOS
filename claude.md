# VibeOS - Claude Context

**IMPORTANT DISK RULES:**
- Never look at the disk
- The disk always works
- `make run` is the only way to compile and run the code - the user will run it
- Trust `make run` - the user will tell you if it is broken
- Do NOT use hdiutil for anything

## Project Overview
VibeOS is a hobby operating system built from scratch for aarch64 (ARM64), targeting QEMU's virt machine. This is a science experiment to see what an LLM can build.

## The Vibe
- **Aesthetic**: Retro Mac System 7 / Apple Lisa (1-bit black & white, Chicago-style fonts, classic Mac UI)
- **Philosophy**: Simple, educational, nostalgic
- **NOT trying to be**: Linux, production-ready, or modern

## Team
- **Human**: Vibes only. Yells "fuck yeah" when things work. Cannot provide technical guidance.
- **Claude**: Full technical lead. Makes all architecture decisions. Wozniak energy.

## Current State (Last Updated: Session 28)
- [x] Bootloader (boot/boot.S) - Sets up stack, clears BSS, jumps to kernel
- [x] Minimal kernel (kernel/kernel.c) - UART output working
- [x] Linker script (linker.ld) - Memory layout for QEMU virt
- [x] Makefile - Builds and runs in QEMU
- [x] Boots successfully! Prints to serial console.
- [x] Memory management (kernel/memory.c) - malloc/free working, dynamic heap sizing
- [x] DTB parsing (kernel/dtb.c) - Detects RAM size at runtime from Device Tree
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
- [x] FAT32 filesystem (kernel/fat32.c) - Read/write, full LFN (long filename) support
- [x] Persistent storage - 64MB FAT32 disk image, mountable on macOS
- [x] Interrupts - GIC-400 working! Keyboard via IRQ, boots at EL3 (Secure)
- [x] Timer - 10ms tick (100Hz), used for uptime tracking
- [x] System Monitor - GUI app showing uptime and memory usage
- [x] TextEdit - GUI text editor with Save As modal
- [x] RTC - PL031 real-time clock at 0x09010000, shows actual date/time
- [x] Date command - /bin/date shows current UTC date/time
- [x] Menu bar - Apple menu with About/Quit, File menu, Edit menu
- [x] About dialog - Shows VibeOS version, memory, uptime
- [x] Power management - WFI-based idle, mouse interrupt-driven, 100Hz UI refresh
- [x] Virtio Sound - Audio playback via virtio-sound device, WAV and MP3 support
- [x] Music Player - GUI music player with album/track browser, pause/resume, progress bar
- [x] Floating point - FPU enabled, context switch saves/restores FP regs, calc uses doubles

## Architecture Decisions Made
1. **Target**: QEMU virt machine, aarch64, Cortex-A72
2. **Memory start**: 0x40000000 (QEMU virt default)
3. **UART**: PL011 at 0x09000000 (QEMU virt default)
4. **Stack**: 64KB, placed in .stack section after BSS
5. **Toolchain**: aarch64-elf-gcc (brew install)
6. **Compiler flags**: -mstrict-align (prevent unaligned SIMD), FPU enabled
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
- boot/boot.S - Entry point, EL3→EL1 transition, BSS clear, .data copy
- kernel/kernel.c - Main kernel code
- kernel/dtb.c/.h - Device Tree Blob parser (RAM detection)
- kernel/memory.c/.h - Heap allocator (malloc/free), dynamic sizing
- kernel/string.c/.h - String/memory functions
- kernel/printf.c/.h - Printf implementation
- kernel/fb.c/.h - Framebuffer driver (ramfb)
- kernel/console.c/.h - Text console (with UART fallback)
- kernel/font.c/.h - Bitmap font
- kernel/keyboard.c/.h - Virtio keyboard driver (interrupt-driven)
- kernel/irq.c/.h - GIC-400 interrupt controller driver
- kernel/vectors.S - Exception vector table
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
- kernel/virtio_sound.c/.h - Virtio sound driver (WAV playback)
- linker.ld - Memory layout (flash + RAM regions)
- Makefile - Build system
- disk.img - FAT32 disk image (created by `make disk`)

### User Directory (userspace programs)
- user/lib/vibe.h - Userspace library header
- user/lib/gfx.h - Shared graphics primitives (header-only)
- user/lib/icons.h - Dock icons and VibeOS logo bitmaps
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
| RAM | Detected via DTB | Works with 256MB-4GB+, heap sized dynamically |
| Disk | 64MB FAT32 | Persistent storage via virtio-blk |
| Interrupts | GIC-400 | Keyboard & mouse via IRQ, boots at EL3 for full GIC access |
| Power | WFI idle | Scheduler sleeps CPU when no work, wakes on interrupt |

## Gotchas / Lessons Learned
- **aarch64 va_list**: Can't pass va_list to helper functions easily. Inline the va_arg handling.
- **QEMU virt machine**: Uses PL011 UART at 0x09000000, GIC at 0x08000000
- **Virtio legacy vs modern**: QEMU defaults to legacy virtio (version 1). Use `-global virtio-mmio.force-legacy=false` to get modern virtio (version 2).
- **Virtio memory barriers**: ARM needs `dsb sy` barriers around device register access.
- **strncpy hangs**: Our strncpy implementation causes hangs in some cases. Use manual loops instead.
- **Static array memset**: Don't memset large static arrays - they're already zero-initialized.
- **GIC Security Groups**: GIC interrupts require matching security configuration. If running in Secure EL1, use Group 0 interrupts. Group 1 interrupts in Secure state return IRQ 1022 (spurious). Boot with `-bios` and `secure=on` to start at EL3 with full GIC register access.
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
- **strtok_r NULL rest**: After the last token, `strtok_r` sets `rest` to NULL (not empty string). Always check `rest && *rest` before dereferencing.
- **Flash/RAM linker split**: When booting via `-bios`, code lives in flash (0x0) but data/BSS must be in RAM (0x40000000). Use separate MEMORY regions in linker script with `AT>` for load addresses. Copy .data from flash to RAM at boot.
- **EL3→EL1 direct**: Can skip EL2 entirely. Set `SCR_EL3` with NS=0 (stay Secure), RW=1 (AArch64), then eret to EL1.
- **WFI in scheduler**: When a process yields and it's the only runnable process, WFI before returning to it. This prevents busy-wait loops from cooking the CPU. The kernel handles idle, not individual apps.
- **Don't double-sleep**: If kernel WFIs on idle, apps shouldn't also sleep_ms() - that causes double delay and sluggish UI. Apps just yield(), kernel handles the rest.
- **FAT32 LFN + GCC -O2**: The LFN entry building code crashes with -O2 optimization. Use -O0 for fat32.c. Symptom: translation fault when writing to valid heap memory. Root cause unknown but likely optimizer generating bad code for the byte-by-byte LFN entry construction.
- **Stack must be above BSS**: As kernel grows, BSS section grows. Stack pointer must be well above BSS end. Originally at 0x40010000, but BSS grew to 0x400290d4 - stack was inside BSS and got zeroed during boot! Moved to 0x40100000 (1MB into RAM).
- **_data_load must be 8-byte aligned**: AArch64 `ldr x3, [x0]` instruction requires 8-byte alignment. If `_data_load` in linker script is not aligned, boot hangs during .data copy loop. Add `. = ALIGN(8);` before `_data_load = .;`.
- **Build with -O0 for safety**: GCC optimization causes subtle bugs in OS code - PIE relocations, LFN construction, possibly virtio drivers. Using -O0 everywhere avoids these issues at cost of larger/slower code.
- **FPU enable**: Set CPACR_EL1.FPEN = 0b11 in boot.S to enable FP/SIMD. Without this, any FP instruction causes an exception.
- **FP context switch**: When FPU is enabled, context_switch must save/restore q0-q31, fpcr, fpsr. The fp_regs array must be 16-byte aligned (stp/ldp q regs require this). Added padding to cpu_context_t to ensure fp_regs is at offset 0x80.
- **-mstrict-align required with FPU**: Without -mgeneral-regs-only, GCC uses SIMD for memcpy/struct copies. Some SIMD loads require aligned addresses. Use -mstrict-align to prevent unaligned SIMD access faults.
- **Kernel stack vs heap collision**: Heap runs from `_bss_end + 0x10000` to `0x41000000`. If kernel stack is inside this range, large allocations (like framebuffer backbuffer) will overwrite the stack. Symptom: local variables corrupted with data like `0x00ffffff` (COLOR_WHITE). Stack was at 0x40100000 (inside heap!). Moved to 0x4F000000 (well above heap and program area).
- **DTB at RAM start**: QEMU places the Device Tree Blob at 0x40000000 (start of RAM). Linker script must start .data/.bss after DTB area (we use 0x40200000, leaving 2MB for DTB).
- **DTB unaligned access**: Reading 32/64-bit values from DTB can cause alignment faults on ARM. Read bytes individually and assemble manually (see `read_be32`/`read_be64` in dtb.c).

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

### Session 17
- **INTERRUPTS FINALLY WORKING!**
  - Root cause found: GIC security groups. Running in Non-Secure EL1 but trying to configure Group 0 registers was a no-op.
  - Solution: Boot at EL3 (Secure) using `-bios` instead of `-kernel`, stay in Secure world
  - Changed QEMU flags: `-M virt,secure=on -bios vibeos.bin`
  - Updated linker script: code at 0x0 (flash), data/BSS in RAM (0x40000000)
  - Updated boot.S: EL3→EL1 transition (skip EL2), copy .data from flash to RAM
  - GIC configured for Group 0 (Secure) interrupts
- **New files:**
  - `kernel/irq.c` / `kernel/irq.h` - GIC-400 driver, timer support
  - `kernel/vectors.S` - Exception vector table for AArch64
- **Keyboard now interrupt-driven:**
  - Registered IRQ handler for virtio keyboard (IRQ 78)
  - No more polling needed for keyboard input
- **Fixed FAT32 bug:**
  - `resolve_path()` was dereferencing NULL `rest` pointer from `strtok_r`
  - Added NULL check: `if (rest && *rest && ...)`
- **Timer ready but disabled:**
  - Timer IRQ handler exists, can enable preemptive multitasking later
  - Currently disabled to keep cooperative model stable
- **Achievement**: Full interrupt support! GIC mystery finally solved after Sessions 2-3 failures!

### Session 18
- **Enabled timer for uptime tracking:**
  - Timer fires at 100Hz (10ms intervals)
  - `timer_get_ticks()` returns tick count since boot
  - Keeping cooperative multitasking (no preemption)
- **Added uptime command (`/bin/uptime`):**
  - Shows hours/minutes/seconds since boot
  - Also shows raw tick count
  - Fixed crash: uptime wasn't in USER_PROGS list, old binary had wrong kapi struct
- **Added memory stats to kapi:**
  - `get_mem_used()` and `get_mem_free()`
  - Heap is ~16MB (between BSS end and program load area at 0x41000000)
- **Built System Monitor GUI app (`/bin/sysmon`):**
  - Classic Mac-style windowed app
  - Shows uptime (updates live)
  - Shows memory usage with progress bar (diagonal stripes pattern)
  - Shows used/free memory in MB
  - Auto-refreshes every ~500ms
- **Gotcha: USER_PROGS list**
  - New userspace programs MUST be added to USER_PROGS in Makefile
  - Old binaries on disk with outdated kapi struct will crash!
- **Achievement**: Timer working! System monitoring! The vibes are immaculate.

- **Built TextEdit (`/bin/textedit`):**
  - Simple GUI text editor - no modes, just type
  - Arrow keys, Home, End, Delete all work
  - Ctrl+S to save
  - Save As modal dialog when no filename set
  - Status bar shows filename, line:col, modified indicator
  - Scrolling for long files
  - Usage: `textedit` or `textedit /path/to/file`
- **Enhanced keyboard driver:**
  - Arrow keys now return special codes (0x100-0x106)
  - Ctrl modifier support (Ctrl+A = 1, Ctrl+S = 19, etc.)
  - Key buffer changed from `char` to `int` to support extended codes
  - Added Home, End, Delete key support
- **TextEdit enhancements:**
  - Line numbers in gray gutter on the left
  - Tab key inserts 4 spaces
  - Auto-close brackets and quotes: `()`, `[]`, `{}`, `""`, `''`
  - C syntax highlighting (detects .c and .h files):
    - Keywords in dark blue (if, else, for, while, return, etc.)
    - Comments in dark green (// and /* */)
    - String literals in dark red
    - Numbers in purple
  - Colors only appear in apps where it makes sense (desktop stays 1-bit B&W)
- **Achievement**: Real text editor with IDE-lite features!

### Session 19
- **Built File Explorer (`/bin/files`):**
  - Windowed GUI file browser
  - Navigate directories (click to select, double-click to enter)
  - Path bar showing current location
  - ".." to go up a directory
  - Right-click context menu with:
    - New File / New Folder (with inline rename UI)
    - Rename (inline text editing)
    - Delete (recursive - works on non-empty directories!)
    - Open with TextEdit
    - Open Terminal Here
  - Keyboard navigation (arrow keys, Enter, Backspace)
- **Added right-click support to desktop:**
  - Desktop now detects right mouse button clicks
  - Passes button info to windowed apps via event data3
- **Implemented recursive directory deletion:**
  - `fat32_delete_recursive()` - deletes files and directories recursively
  - Walks directory tree, deletes children first, then parent
  - Exposed via `vfs_delete_recursive()` and kapi
- **Improved create file/folder UX:**
  - "New File" / "New Folder" now shows inline rename field immediately
  - Type the name and press Enter to actually create
  - Press Escape to cancel
  - No more "untitled" / "newfolder" placeholder names
- **Achievement**: Full-featured file explorer!

### Session 21
- **Power management overhaul - CPU no longer cooks the host!**
  - Problem: Desktop was redrawing at infinite FPS, QEMU using 150% CPU
  - Root cause: Busy-wait loops everywhere, no sleeping
- **Mouse now interrupt-driven:**
  - Added `mouse_get_irq()` and `mouse_irq_handler()` (like keyboard)
  - Registered mouse IRQ in kernel.c
- **Added sleep functions to kapi:**
  - `wfi()` - ARM Wait For Interrupt instruction
  - `sleep_ms(ms)` - Sleep for at least N milliseconds using timer ticks
- **Kernel-level idle via WFI:**
  - When process yields and it's the only runnable process, scheduler calls WFI
  - CPU sleeps until next interrupt (timer at 100Hz, keyboard, or mouse)
  - This is the RIGHT approach - kernel handles idle, not apps
- **Removed busy-wait delays from games:**
  - Snake and Tetris had `delay()` functions doing NOPs
  - Replaced with `sleep_ms()` for proper game tick timing
- **Fixed recovery shell:**
  - Was busy-looping on `keyboard_getc()` with no sleep
  - Now WFIs when no input available
- **UI apps just yield():**
  - Desktop, calc, files, textedit, term, sysmon - all just call `yield()`
  - Kernel WFIs if nothing else to do, wakes on interrupt
  - Effective frame rate: 100fps (timer is 100Hz)
- **Result:**
  - CPU usage dropped from 150% to ~10% idle
  - UI runs at smooth 100fps
  - Mouse and keyboard remain responsive (interrupt-driven)
- **Achievement**: Proper power management! VibeOS is now a good citizen.

### Session 22
- **FAT32 Long Filename (LFN) Writing - COMPLETE!**
  - Can now create files/directories with any name length (up to 255 chars)
  - Implemented `needs_lfn()` - detects when LFN is required (lowercase, length, special chars)
  - Implemented `generate_basis_name()` - creates 8.3 basis from long name
  - Implemented `generate_short_name()` - creates unique 8.3 name with ~1, ~2, etc. suffix
  - Implemented `build_lfn_entry()` - constructs LFN directory entries with UTF-16LE encoding
  - Implemented `find_free_dir_entries()` - finds N consecutive free slots for LFN + short entry
  - Updated `create_dir_entry()` - writes LFN entries in reverse order followed by 8.3 entry
  - Updated `fat32_rename()` - deletes old entries (including LFN), creates new with LFN support
  - Updated `fat32_delete()`, `fat32_delete_dir()`, `fat32_delete_recursive()` - properly deletes LFN entries
  - Added `delete_dir_entry_with_lfn()` - finds and marks all associated LFN entries as deleted
- **Build fix**: fat32.c requires -O0 (added to Makefile)
  - GCC -O2 generates bad code for LFN byte manipulation
  - Caused translation faults when writing to valid heap memory
- **Achievement**: Full LFN support! `touch "my long filename.txt"` works!

### Session 23
- **Virtio Sound Driver - Audio playback working!**
  - Built complete virtio-sound driver (kernel/virtio_sound.c, kernel/virtio_sound.h)
  - Device ID 25, virtqueues: controlq (0), eventq (1), txq (2), rxq (3)
  - PCM stream lifecycle: set_params → prepare → start → submit data → stop
  - WAV file parsing with format/rate detection
  - Supports 44100Hz, 48000Hz sample rates; 16-bit stereo/mono
- **New files:**
  - `kernel/virtio_sound.c` - Full virtio sound driver (~600 lines)
  - `kernel/virtio_sound.h` - Public API: play_wav, stop, is_playing
  - `user/bin/play.c` - Userspace play command
- **Added to kapi:**
  - `sound_play_wav(data, size)` - Play WAV from memory
  - `sound_stop()` - Stop playback
  - `sound_is_playing()` - Check if playing
- **QEMU flags updated:**
  - Added `-device virtio-sound-device,audiodev=audio0 -audiodev coreaudio,id=audio0`
- **Critical boot fixes:**
  - Stack was at 0x40010000, inside BSS (0x40001000-0x400290d4) - getting overwritten!
  - Moved stack to 0x40100000 (1MB into RAM) in boot.S
  - `_data_load` at 0x1a9bb was NOT 8-byte aligned - caused hang during data copy
  - Added `. = ALIGN(8);` before `_data_load = .;` in linker.ld
- **Build change:**
  - Changed to `-O0` everywhere (both kernel and userspace) to avoid optimization issues
- **Fixed slow file loading:**
  - `play.c` was reading file twice: once to get size, once to load data
  - Added `file_size()` to kapi - returns vfs_node->size directly
  - Now files load instantly instead of 7+ seconds
- **Achievement**: VibeOS can play audio! `play /beep.wav` works!

### Session 24
- **Floating Point Support - COMPLETE!**
  - Enabled FPU in boot.S: `mov x0, #(3 << 20); msr cpacr_el1, x0`
  - Removed `-mgeneral-regs-only` from CFLAGS (was blocking all FP)
  - Added `-mstrict-align` to prevent unaligned SIMD access faults
  - GCC now uses SIMD for memcpy/struct ops, which is fine with strict-align
- **Context switch updated for FP state:**
  - Extended cpu_context_t: added fpcr, fpsr, _pad, fp_regs[64]
  - _pad ensures fp_regs is at offset 0x80 (16-byte aligned for stp/ldp q regs)
  - context.S now saves/restores all 32 Q registers (q0-q31) plus fpcr/fpsr
- **Calculator now uses floating point:**
  - Changed display_value/pending_value from int to double
  - Added decimal point button (replaced C button)
  - Added float_to_str() for display (no printf %f available)
  - Clear via 'c'/'C' keyboard key
  - Can now do: 10 / 3 = 3.333333, 3.14159 * 2, etc.
- **MAJOR BUG FIX: Kernel stack vs heap collision**
  - Symptom: Process exit crashed with `0x00ffffff` in return address
  - Root cause: Kernel stack at 0x40100000 was INSIDE heap range (0x4003c0d4 - 0x41000000)
  - Large heap allocations (backbuffer) overwrote stack with framebuffer data
  - Debug showed local variable `old_pid` corrupted from -1 to 0x00ffffff (COLOR_WHITE)
  - Fix: Moved stack to 0x4F000000, well above heap and program load area
- **Memory layout clarified:**
  ```
  0x40000000 - 0x40200000: DTB (Device Tree Blob, placed by QEMU)
  0x40200000 - 0x40237000: Kernel .data/.bss
  0x40247000 - 0x4E000000: Heap (dynamic, up to stack - 1MB)
  0x4E000000+:             Program load area (after heap)
  0x4F000000:              Kernel stack (grows down, hardcoded)
  RAM end:                 Detected from DTB (256MB-4GB+)
  ```
- **Memory collision issue (MOSTLY FIXED in Session 26)**
  - Was: hardcoded magic numbers everywhere that collided as kernel grew
  - Now: heap and program areas are dynamic based on DTB-detected RAM
  - Stack is still hardcoded at 0x4F000000 (works for 256MB+ systems)
  - Heap is bounded by stack address, so can't overflow into stack anymore
- **Achievement**: Floating point works! Calculator does decimals! Processes exit cleanly!

- **Enabled -O3 optimization!**
  - Changed CFLAGS and USER_CFLAGS from -O0 to -O3
  - Everything works except kernel's `fat32_delete_recursive()` which breaks with -O3
  - Solution: Moved recursive delete logic to userspace (rm.c and files.c)
  - Kernel only needs to delete single files and empty directories
  - Userspace iterates with `readdir()` and deletes children before parent
- **Userspace recursive delete:**
  - `rm -r` now implements recursion in userspace
  - Files app delete action uses same userspace implementation
  - Works correctly with -O3 optimization
- **Achievement**: Full -O3 optimization across kernel and userspace!

### Session 25
- **Desktop modularization - code cleanup!**
  - Extracted icon bitmaps to `user/lib/icons.h` (~500 lines of bitmap data)
  - Created VibeOS logo (stylized "V") to replace Apple logo
  - Created shared graphics library `user/lib/gfx.h` (header-only, ~130 lines)
  - Updated all GUI apps to use gfx.h instead of duplicated drawing code:
    - desktop.c, calc.c, sysmon.c, files.c, textedit.c
  - Removed ~250 lines of duplicated buf_*/bb_* drawing functions across apps
  - desktop.c reduced from ~1500 to ~1200 lines
- **New files:**
  - `user/lib/icons.h` - All 32x32 dock icons + 16x16 VibeOS logo
  - `user/lib/gfx.h` - gfx_ctx_t context, put_pixel, fill_rect, draw_char, draw_string, draw_rect, patterns
- **Architecture note:**
  - gfx.h is header-only with static inline functions - no Makefile changes needed
  - Apps use macros to alias old function names (buf_*, bb_*) to new gfx_* calls
  - Zero runtime overhead, compiler inlines everything
- **Achievement**: Cleaner codebase! Shared graphics primitives!

### Session 26
- **Device Tree Blob (DTB) parsing - RAM detection at runtime!**
  - Built DTB parser (`kernel/dtb.c`, `kernel/dtb.h`)
  - Parses QEMU's device tree to find memory node with base/size
  - Tested with 256MB, 1GB, 4GB - all detected correctly
- **Dynamic memory layout - no more hardcoded heap size!**
  - Linker script now starts RAM at 0x40200000 (preserves DTB at 0x40000000)
  - Heap size computed at runtime: from BSS end to (stack - 1MB buffer)
  - Program load area follows heap_end dynamically
  - Framebuffer now allocated via malloc instead of hardcoded address
- **Memory layout now:**
  - DTB preserved at 0x40000000 (up to 2MB reserved)
  - Kernel .data/.bss at 0x40200000+
  - Heap from BSS end to 0x4E000000 (~238MB on 256MB system)
  - Programs load after heap
  - Stack still hardcoded at 0x4F000000 (works for 256MB+ systems)
- **Key insight:** One shared address space (Win3.1 model)
  - Heap is shared by kernel + all apps
  - App static variables (`int x = 3`) live in ELF's .data section, loaded into program area
  - Only explicit `malloc()` uses the heap
- **Gotcha: DTB unaligned access**
  - Direct pointer casts to read 32/64-bit values cause alignment faults
  - Must read bytes individually and assemble (see `read_be32`/`read_be64`)
- **Achievement**: RAM detected dynamically! No more arbitrary 256MB cap!

### Session 27
- **MP3 Playback - minimp3 ported!**
  - Integrated minimp3 single-header decoder into userspace
  - Created stub headers `user/lib/stdlib.h` and `user/lib/string.h` for freestanding build
  - Added `memcmp`, `memmove` to string.h stub
  - `#define MINIMP3_NO_SIMD` to avoid NEON complexity (can enable later for perf)
- **Refactored sound API:**
  - Moved WAV parsing from kernel to userspace (kernel shouldn't parse file formats)
  - Added `sound_play_pcm(data, samples, channels, rate)` to kapi - flexible raw PCM playback
  - Kernel's `hz_to_rate_index()` converts Hz to virtio rate constants
- **Mono to stereo conversion:**
  - virtio-sound doesn't support mono playback
  - play.c converts mono MP3s to stereo by duplicating samples
- **Async (non-blocking) playback!**
  - Added `sound_play_pcm_async()` - starts playback and returns immediately
  - Added `virtio_sound_pump()` - called from timer interrupt (100Hz) to feed audio chunks
  - Audio plays in background while user continues using the system
  - PCM buffer must stay allocated during playback (memory is "orphaned" but OK for CLI)
- **play command upgraded:**
  - `play /file.wav` - plays WAV files
  - `play /file.mp3` - decodes and plays MP3 files
  - Auto-detects format by extension or magic bytes (RIFF, ID3, MP3 sync)
  - Shows format info: sample rate, channels, duration, bitrate
- **New/modified files:**
  - `vendor/minimp3.h` - Single-header MP3 decoder (already existed)
  - `user/lib/stdlib.h` - Empty stub for minimp3
  - `user/lib/string.h` - memcmp, memmove for minimp3
  - `user/bin/play.c` - WAV/MP3 player with async playback
  - `kernel/virtio_sound.c` - Added async playback, pump function
  - `kernel/irq.c` - Timer calls virtio_sound_pump()
- **Achievement**: MP3 playback with non-blocking audio! Music plays while you work!

### Session 28
- **Built GUI Music Player (`/bin/music`)!**
  - Classic Mac System 7 1-bit B&W aesthetic
  - Two-panel layout: album sidebar (160px) + track list
  - Scans `/home/user/Music/` for album folders containing MP3s
  - Click album to load tracks, double-click track to play
  - Playback controls: |< (back), Play/Pause, >| (next)
  - Progress bar with elapsed/total time display
  - Volume slider (visual only for now)
  - Keyboard shortcuts: Space (play/pause), N/P (next/prev), arrows (select), Enter (play)
  - Auto-advances to next track when song ends
- **Implemented pause/resume in kernel:**
  - Added `virtio_sound_pause()` - stops stream but keeps playback state
  - Added `virtio_sound_resume()` - reconfigures and restarts from paused position
  - Added `virtio_sound_is_paused()` - check pause state
  - New kapi functions: `sound_pause`, `sound_resume`, `sound_is_paused`
  - Pause saves offset, resume continues from where it left off
- **Fixed progress bar duration bug:**
  - `pcm_samples * 1000` was overflowing uint32_t for songs > 71 seconds
  - Changed to `(uint64_t)pcm_samples * 1000` for 64-bit multiplication
  - Added `uint64_t` typedef to `user/lib/vibe.h`
- **Music player icon added to dock:**
  - 32x32 musical note icon in `user/lib/icons.h`
  - Click dock icon to launch music player
- **New/modified files:**
  - `user/bin/music.c` - Full music player application (~800 lines)
  - `user/lib/icons.h` - Added music_icon bitmap
  - `user/bin/desktop.c` - Added Music app to dock
  - `kernel/virtio_sound.c` - Pause/resume support, state tracking
  - `kernel/virtio_sound.h` - New function declarations
  - `kernel/kapi.c`, `kernel/kapi.h` - Exposed pause/resume to userspace
  - `user/lib/vibe.h` - Added uint64_t, pause/resume kapi functions
- **Achievement**: Full-featured music player! Pause, resume, progress tracking all work!

### Session 29
- **Improved Terminal Emulator (`/bin/term`):**
  - 500-line scrollback buffer (ring buffer implementation)
  - Mouse drag scrolling - click and drag to scroll through history
  - Scroll indicator `[N]` in top-right when scrolled back
  - Auto-scroll to bottom on keystroke
  - Input buffer now uses `int` to preserve special keys (0x100+)
  - Form feed (\f) support for clear screen
- **Improved Shell (`/bin/vibesh`) with readline-like editing:**
  - Command history (50 commands) with Up/Down arrow navigation
  - `!!` to repeat last command
  - Ctrl+C - clear current line (prints ^C)
  - Ctrl+U - clear line before cursor
  - Ctrl+D - exit shell (EOF) when line empty
  - Ctrl+L - clear screen
  - Ctrl+R - reverse incremental search through history
  - Tab completion for commands (/bin) and file paths
  - `clear` builtin command
  - Left/Right arrows for cursor movement
  - Home/End to jump to start/end of line
  - Delete key support
- **Added special key constants to vibe.h:**
  - KEY_UP, KEY_DOWN, KEY_LEFT, KEY_RIGHT, KEY_HOME, KEY_END, KEY_DELETE
- **Achievement**: Proper terminal with scrollback and readline-style shell!

**NEXT SESSION TODO:**
- Make music loading non-blocking (show spinner, decode in chunks)
- Maybe DOOM?
