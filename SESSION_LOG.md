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

### Session 30
- **NETWORKING - VibeOS is on the internet!**
- **Virtio-net driver (`kernel/virtio_net.c`):**
  - Device ID 1, virtqueues: 0=receiveq, 1=transmitq
  - Reads MAC address from config space
  - Pre-populates RX buffers for async receive
  - Interrupt-driven packet notification (IRQ handler just acks, doesn't consume)
- **Network stack (`kernel/net.c`, `kernel/net.h`):**
  - Ethernet frame send/receive
  - ARP table (16 entries) with request/reply handling
  - IP layer with checksum calculation and routing
  - ICMP echo request/reply (ping)
  - Automatic gateway ARP resolution
- **Network configuration (QEMU user-mode NAT):**
  - Our IP: 10.0.2.15
  - Gateway: 10.0.2.2
  - DNS: 10.0.2.3
- **Ping command (`/bin/ping`):**
  - Parses IP addresses from command line
  - Sends 4 ICMP echo requests with 1 second timeout
  - Shows reply/timeout for each, plus statistics
- **QEMU flags updated:**
  - Added `-device virtio-net-device,netdev=net0 -netdev user,id=net0`
- **kapi additions:**
  - `net_ping(ip, seq, timeout_ms)` - Blocking ping
  - `net_poll()` - Process incoming packets
  - `net_get_ip()`, `net_get_mac()` - Get our addresses
- **New files:**
  - `kernel/virtio_net.c` (~400 lines) - Virtio network driver
  - `kernel/virtio_net.h` - Driver header
  - `kernel/net.c` (~450 lines) - Network stack
  - `kernel/net.h` - Network stack header
  - `user/bin/ping.c` - Ping command
- **Bug fixed: main() argument order**
  - crt0.S passes: `main(kapi_t*, argc, argv)`
  - ping.c had wrong order, caused crash on kapi access
- **Achievement**: Can ping 1.1.1.1 (Cloudflare) from VibeOS! Packets traverse the real internet!

### Session 31
- **UDP, DNS, TCP, and HTTP - Full network stack complete!**
- **UDP implementation (`kernel/net.c`):**
  - UDP listener table with callback system (8 ports)
  - `udp_bind(port, callback)` / `udp_unbind(port)` - register listeners
  - `udp_send(ip, src_port, dst_port, data, len)` - send packets
  - Checksum optional (set to 0, valid for IPv4)
- **DNS resolver (`kernel/net.c`):**
  - `dns_resolve(hostname)` - returns IP address or 0
  - Builds DNS query packets with proper QNAME encoding
  - Parses A records from response
  - Uses QEMU's built-in DNS at 10.0.2.3
- **Ping updated:**
  - `ping google.com` now works (resolves hostname first)
  - Auto-detects IP vs hostname input
- **TCP implementation (`kernel/net.c`, ~430 lines):**
  - Full TCP state machine: CLOSED → SYN_SENT → ESTABLISHED → FIN_WAIT → etc.
  - TCP pseudo-header checksum calculation
  - 3-way handshake in `tcp_connect()`
  - 8KB receive ring buffer per socket
  - Proper FIN/ACK handling for graceful close
  - 8 concurrent sockets supported
  - MSS-aware segmentation (1400 byte chunks)
- **TCP API:**
  - `tcp_connect(ip, port)` - connect to server, returns socket handle
  - `tcp_send(sock, data, len)` - send data, returns bytes sent
  - `tcp_recv(sock, buf, maxlen)` - receive data (non-blocking)
  - `tcp_close(sock)` - graceful close with FIN
  - `tcp_is_connected(sock)` - check connection state
- **Fetch command (`/bin/fetch`):**
  - Usage: `fetch <hostname> [path]`
  - Resolves hostname, opens TCP connection to port 80
  - Sends HTTP/1.0 GET request
  - Prints response body
  - Example: `fetch google.com /` → got 301 redirect!
- **kapi additions:**
  - `dns_resolve(hostname)` - DNS resolution
  - `tcp_connect`, `tcp_send`, `tcp_recv`, `tcp_close`, `tcp_is_connected`
- **Achievement**: Made an HTTP request to google.com and got a real response!

### Session 32
- **Web Browser (`/bin/browser`) - GUI web browser!**
- **Improved fetch command:**
  - URL parsing (host, port, path extraction)
  - HTTP header parsing (status code, Content-Length, Location, Content-Type)
  - Redirect following (301, 302, 307, 308) with max 5 redirects
  - Handles both absolute and relative redirect URLs
- **Browser features:**
  - GUI window with address bar
  - Click address bar to edit URL, Enter to navigate
  - HTML parser with tag handling:
    - Strips `<script>`, `<style>`, `<head>` content
    - Handles headings (h1-h6), bold, links, lists, paragraphs
    - Decodes HTML entities (&amp;, &lt;, &gt;, etc.)
  - Text rendering with word wrap
  - Scrolling with Up/Down arrows, j/k keys, Space for page down
  - Scrollbar indicator for long pages
  - Status bar showing loading/ready state
  - Keyboard shortcuts: G (go to URL), R (reload)
- **Browser icon added to dock** (globe icon)
- **HTTP quirks discovered:**
  - Many sites force HTTPS (Wikipedia, YouTube, Amazon, DuckDuckGo)
  - Some sites reject simple User-Agent strings
  - Changed User-Agent to Chrome on Windows for compatibility
  - Works well on: httpforever.com, stallman.org, info.cern.ch
  - HTTPS would require full TLS implementation (~thousands of lines of crypto)
- **Debugging network issues:**
  - `yield()` during HTTP receive caused timing issues with desktop
  - `sleep_ms()` works correctly for polling
  - Different sites have different response timing behaviors
- **New files:**
  - `user/bin/browser.c` (~730 lines) - Full GUI web browser
  - `user/lib/icons.h` - Added browser (globe) icon
- **Achievement**: VibeOS has a web browser! Can browse HTTP sites with HTML rendering!

### Session 34
- **Refactored browser into multi-file structure:**
  - Split 1400-line `user/bin/browser.c` into modular components
  - New directory: `user/bin/browser/`
  - `str.h` - String helper functions (str_len, str_cpy, str_ncpy, str_eqn, str_ieqn, parse_int)
  - `url.h` - URL parsing (url_t struct, parse_url, resolve_url)
  - `http.h` - HTTP client (http_response_t, http_get, parse_headers, is_redirect)
  - `html.h` - HTML parser (text_block_t, style_state_t, parse_html, entity decoding)
  - `main.c` - Browser UI, rendering, and event loop
- **Updated Makefile for multi-file userspace builds:**
  - Added `USER_PROGS_MULTIFILE` list for programs built from directories
  - Browser compiles from `user/bin/browser/main.c` with all headers
  - Pattern supports future multi-file apps
- **Architecture note:** Used header-only libraries (static inline) for simplicity - no separate .c files needed
- **Achievement**: Browser code now organized and maintainable!

  Sessions 33-35 (Various improvements)

  - Browser enhancements:
    - Back button with history stack - navigate to previous pages
    - Link following - click <a href> links to navigate
    - Fixed inline text rendering bug (links were breaking to new lines)
    - Ordered list (<ol>) support
  - HTTPS/TLS 1.2 - COMPLETE!
    - Ported TLSe library (added to vendor/)
    - Created kernel libc stubs (kernel/libc/) to satisfy TLSe dependencies:
        - arpa/inet.h, assert.h, ctype.h, errno.h, limits.h, signal.h
      - stdio.h, stdlib.h, string.h, time.h, unistd.h, wchar.h
    - Built kernel/tls.c wrapper (~420 lines) exposing TLS to userspace
    - Added tls_connect, tls_send, tls_recv, tls_close to kapi
    - Updated /bin/fetch - now supports https:// URLs
    - Updated /bin/browser - HTTPS sites work!
    - Added memcmp, memmove to kernel string.c
    - Achievement: Can browse google.com, real HTTPS sites!
  - Resolution upgrade: 800x600 → 1024x768
  - Window resize: Draggable window edges for resizing
  - System Monitor expanded:
    - Shows used/unused heap
    - Process list
    - Sound playing status
  - MP3 loading improvements: Single-pass decode with loading state indicators

### Session 36
- **Fixed critical memory corruption bug in TTF rendering:**
  - Browser was consuming ALL available RAM (tested 512MB and 8GB - both exhausted!)
  - sysmon showed only 73 allocations, ~11MB heap "used", but 0MB "free" - contradiction
  - Root cause identified with help from Gemini: **buffer overflow in `apply_italic()`**
  - Bug: `apply_italic` was calculating `extra_w` again on top of already-expanded buffer
    - Called with `alloc_w` (already includes italic space)
    - Internally calculated `out_w = w + extra_w` (doubling the expansion)
    - `memcpy(bitmap, temp_bitmap, out_w * h)` wrote past buffer end
  - This corrupted heap metadata, breaking the free list traversal
  - Fixed by passing original glyph width separately from stride
- **Enhanced sysmon with memory debugging:**
  - Added `get_heap_start()`, `get_heap_end()`, `get_stack_ptr()`, `get_alloc_count()` to kapi
  - sysmon now shows: heap bounds, heap size, stack pointer, allocation count
  - Helped diagnose the memory corruption (heap looked full because free list was broken)
- **Cache eviction fix:**
  - TTF glyph cache eviction now frees bitmap allocations before resetting
  - Previously leaked all 128 bitmaps on each cache wrap
- **Expanded font size caches:**
  - Added sizes 14, 18, 20, 28 to match browser heading sizes
  - Prevents cache misses for common sizes
- **Known issue:** Italic rendering is still broken (garbled output)
  - The buffer overflow is fixed, but the shear logic needs more work
  - Bold works fine, italic produces garbage
- **Files changed:**
  - `kernel/ttf.c` - Fixed apply_italic buffer overflow, cache eviction
  - `kernel/memory.c` - Added debug functions
  - `kernel/memory.h` - Declared debug functions
  - `kernel/kapi.c`, `kernel/kapi.h` - Added memory debug to kapi
  - `user/lib/vibe.h` - Added memory debug functions
  - `user/bin/sysmon.c` - Added Memory Debug section

### Session 37
- **CSS Engine - Full CSS support for the browser!**
- **New files:**
  - `user/bin/browser/css.h` (~950 lines) - Complete CSS parser and style engine:
    - CSS value types (length units: px, em, rem, %)
    - Display modes (block, inline, inline-block, none, table, flex, list-item)
    - Float, position, visibility properties
    - Box model (width, height, margin, padding)
    - Text properties (color, background-color, font-size, font-weight, font-style, text-align, text-decoration, white-space, vertical-align)
    - CSS selector parsing (tag, .class, #id, [attr], *, combinators: >, +, ~, descendant)
    - Specificity calculation for cascade
    - `<style>` block parsing
    - Inline style attribute parsing
    - Pseudo-selector skipping (:hover, :focus, :not(), etc.)
  - `user/bin/browser/dom.h` (~400 lines) - Proper DOM tree structure:
    - DOM nodes (element and text types)
    - Tree structure (parent, children, siblings)
    - Attribute storage (id, class, style, href)
    - User-agent default styles for HTML elements
    - Style inheritance (color, font-size, etc. inherit from parent)
    - Computed style calculation (cascade: UA → stylesheet → inline)
    - Selector matching against DOM nodes
- **Updated files:**
  - `user/bin/browser/html.h` - Rewrote HTML parser to build DOM tree:
    - Creates proper tree structure instead of flat list
    - Extracts `<style>` blocks and parses them
    - Stores id, class, style, href attributes on nodes
    - Computes styles after parsing
    - `dom_to_blocks()` converts DOM to flat render list (legacy compatibility)
    - Fixed: href attribute now stored properly (was placeholder)
    - Fixed: Duplicate newline prevention (tracks last_was_newline)
  - `user/bin/browser/main.c` - Updated renderer:
    - Uses CSS colors from text blocks
    - Uses CSS font-size for TTF rendering
    - Uses CSS margin-left for indentation
    - Skips blocks with `display:none` (is_hidden flag)
    - Fixed TTF word wrapping (was going off-screen):
      - Estimates char width based on font size (not fixed 8px)
      - Renders word-by-word with wrap check
      - Wraps to next line if word would exceed right edge
  - `user/lib/crt0.S` - Added memcpy, memmove, memset implementations:
    - GCC generates calls to these for struct copies with -O3
    - Assembly implementations for AArch64
- **Bug fixes:**
  - CSS parser infinite loop on Wikipedia - selectors starting with `:` caused hang
  - Added pseudo-selector skipping (`:hover`, `:not()`, `::before`, etc.)
  - Added safety: skip unknown characters, handle malformed rules gracefully
  - Links were untappable - href wasn't being stored/passed through
- **Wikipedia now renders!** (was hanging before)
- **Text wraps properly** (was going off-screen)
- **Reduced whitespace** (was adding double newlines everywhere)
- **Links clickable again** (href properly stored in DOM and passed to renderer)
- **Achievement**: CSS engine complete! Wikipedia is usable!

### Session 38
- **Image Viewer (`/bin/viewer`) - View images from the file manager!**
  - Added in previous commit (e739b6b)
  - Supports PNG, JPG, BMP via stb_image
  - Center image in window, scale if too large
  - Arrow keys or click to navigate prev/next in same directory
- **Music Player - Single file mode!**
  - Can now open individual audio files: `music /path/to/song.mp3`
  - Supports both MP3 and WAV formats
  - When opened with file argument:
    - Enters compact "Now Playing" mode (350x200 window)
    - Shows filename centered with playback controls
    - Just Play/Pause button (no prev/next)
    - Auto-plays immediately on launch
  - When opened without arguments: Album browser mode (existing behavior)
  - New `play_file()` function handles both MP3 and WAV decoding
  - WAV support: Parses RIFF header, extracts PCM, handles mono→stereo conversion
  - `ends_with()` helper for case-insensitive extension checking
  - Prep work for file manager associations (double-click .mp3/.wav opens Music player)
- **Achievement**: Music player ready for file associations!

- **File Manager - Default App Associations!**
  - Double-click files to open them with the appropriate app
  - Smart file type detection:
    - Image extensions (`.png`, `.jpg`, `.jpeg`, `.bmp`, `.gif`) → `/bin/viewer`
    - Audio extensions (`.mp3`, `.wav`) → `/bin/music`
    - Everything else: content-based text detection
  - `is_text_file()` - reads first 512 bytes, checks for null bytes and control chars
    - Null byte = binary file
    - >10% non-printable chars = binary file
    - Otherwise = text file → `/bin/textedit`
  - No giant extension list needed - just 9 media extensions, rest is auto-detected
  - Binary files silently ignored (no app association)
- **Added `spawn_args()` to kernel API:**
  - Like `spawn()` but with argc/argv support
  - Non-blocking: parent continues immediately
  - Files app now opens files concurrently (can open multiple at once)
  - `kernel/kapi.c`, `kernel/kapi.h`, `user/lib/vibe.h` updated
- **Achievement**: File associations complete! Double-click any file to open it!

- **TextEdit - Unsaved Changes Warning:**
  - Shows dialog when closing with unsaved changes
  - "You have unsaved changes. Save before closing?"
  - Three buttons: [Save] [Don't Save] [Cancel]
  - Keyboard shortcuts: S/Enter=Save, D/N=Don't Save, Esc=Cancel
  - Mouse hover highlighting on buttons
  - Smart flow: if no filename, opens Save As first, then closes after save
  - `pending_close` state tracks waiting for save to complete
- **Achievement**: TextEdit warns before losing work!

### Session 39
- **V1 COMPLETE - Raspberry Pi Zero 2W Port Begins!**
- **Hardware Abstraction Layer (HAL) created:**
  - `kernel/hal/hal.h` - Common interface for platform-specific hardware
  - `kernel/hal/qemu/` - QEMU virt machine implementations
    - `fb.c` - ramfb via fw_cfg
    - `serial.c` - PL011 UART
    - `platform.c` - Platform info
  - `kernel/hal/pizero2w/` - Raspberry Pi Zero 2W implementations
    - `fb.c` - VideoCore mailbox framebuffer
    - `serial.c` - Mini UART (GPIO 14/15)
    - `platform.c` - Platform info
- **Updated core kernel to use HAL:**
  - `kernel/fb.c` - Now calls `hal_fb_init()` and `hal_fb_get_info()`
  - `kernel/kernel.c` - UART functions now wrap HAL serial calls
- **Pi-specific boot code:**
  - `boot/boot-pi.S` - Entry point for Pi (loads at 0x80000, drops from EL2→EL1)
  - `linker-pi.ld` - Memory layout for Pi (no separate flash region)
- **Build system updated:**
  - `TARGET=qemu` (default) - Builds `build/vibeos.bin`
  - `TARGET=pi` or `make pi` - Builds `build/kernel8.img`
  - HAL files automatically selected based on target
- **SD card installer script:**
  - `scripts/install-pi.sh` - Downloads Pi firmware, formats SD, installs VibeOS
  - `make install-pi DISK=disk5s2` - One-command install
  - Downloads bootcode.bin, start.elf, fixup.dat from official Pi firmware repo
  - Creates config.txt with arm_64bit=1
- **FIRST BOOT ON REAL HARDWARE - IT WORKS!**
  - Pi Zero 2W boots to VibeOS splash screen!
  - VideoCore mailbox framebuffer working
  - Console text rendering working
  - Full boot sequence completes
  - Drops to recovery shell (expected - no disk/keyboard drivers yet)
- **What works on Pi:**
  - Boot sequence (EL2→EL1 transition)
  - FPU initialization
  - Exception vectors
  - Framebuffer via VideoCore mailbox
  - Console and font rendering
  - Memory allocator
  - Printf/kernel init
- **What's missing (expected - need new drivers):**
  - Block device (need SDHCI driver for SD card)
  - Keyboard/Mouse (need USB HID stack)
  - Sound/Network (skip for v1 Pi port)
- **Achievement**: VibeOS boots on real hardware! First try!

### Session 40
- **USB Host Driver for Raspberry Pi Zero 2W!**
- **DWC2 (DesignWare USB 2.0) controller driver (`kernel/hal/pizero2w/usb.c`):**
  - ~1100 lines of bare-metal USB host implementation
  - Based on Linux dwc2 driver documentation (generated via Gemini analysis)
  - Full register definitions for Global, Host, and Channel registers
  - Slave mode (no DMA) - CPU handles FIFO read/write
- **USB initialization sequence:**
  - Power on USB controller via VideoCore mailbox (device ID 3)
  - Core soft reset (wait for AHBIDLE, trigger CSFTRST, wait for completion)
  - PHY configuration (UTMI+ 8-bit interface, no PHYSEL for Pi's HS PHY)
  - Force host mode via GUSBCFG
  - FIFO sizing (RxFIFO 256 words, TxFIFO 256 words each)
  - Frame interval configuration (60MHz PHY clock, HFIR=60000)
- **Port control:**
  - VBUS power on via HPRT0.PRTPWR
  - Device connection detection via HPRT0.PRTCONNSTS
  - Port reset (50ms assert, then deassert)
  - Speed detection from HPRT0.PRTSPD (Full Speed detected)
- **Control transfers (SETUP/DATA/STATUS):**
  - Channel-based transfers using HCCHAR, HCTSIZ, HCINT
  - Proper NAK retry handling with channel re-enable
  - Multi-packet IN transfers (re-enable channel after each packet)
  - FIFO read/write with GRXSTSP status parsing
- **USB enumeration working:**
  - GET_DESCRIPTOR (device) - VID/PID and max packet size
  - SET_ADDRESS - assign device address 1
  - GET_DESCRIPTOR (configuration) - full config with interfaces
  - SET_CONFIGURATION - activate device
  - HID keyboard detected!
- **Key debugging insights (with help from Gemini):**
  - **Babble error cause #1:** MPS=8 for Full Speed devices - should be 64!
    - FS devices send up to 64 bytes per packet, 8 causes babble on byte 9
  - **Babble error cause #2:** Wrong PHY clock setting
    - Pi uses UTMI+ PHY at 60MHz, not dedicated 48MHz FS PHY
    - FSLSPCLKSEL must be 0 (30/60MHz), not 1 (48MHz)
  - **Multi-packet timeout:** Need to re-enable channel after each IN packet
    - Slave mode requires explicit channel re-enable for each packet
    - Fixed by re-enabling on ACK and IN_COMPLETE events
- **HAL integration:**
  - Added `hal_usb_init()` and `hal_usb_keyboard_poll()` to hal.h
  - QEMU stub returns -1 (uses virtio input instead)
  - Pi calls USB init during kernel startup
- **What works:**
  - Device detection and enumeration
  - Reading device/config descriptors
  - HID keyboard identification
- **What's next:**
  - Interrupt transfers for HID reports (keyboard input)
  - HID report parsing
  - Wire up to HAL keyboard interface
- **New files:**
  - `kernel/hal/pizero2w/usb.c` - DWC2 USB host driver
  - `docs/rpi_usb.md` - Comprehensive USB implementation documentation
- **Achievement**: USB enumeration works on real Pi hardware! Keyboard detected!

### Session 41
- **INTERRUPTS WORK ON REAL PI HARDWARE!**
- **Two-level interrupt controller architecture implemented:**
  - **ARM Local Controller (0x40000000):** Root controller for BCM2836/2837
    - Per-core interrupt routing
    - LOCAL_CONTROL, LOCAL_PRESCALER for timer config
    - LOCAL_TIMER_INT_CTRL0 for enabling timer IRQs
    - LOCAL_IRQ_PENDING0 for reading pending interrupts
    - Bit 1 = CNTPNSIRQ (ARM Generic Timer)
    - Bit 8 = GPU interrupt (from BCM2835 IC)
  - **BCM2835 IC (0x3F00B200):** GPU peripheral interrupts
    - Three banks: Bank 0 (ARM local), Bank 1 (GPU 0-31), Bank 2 (GPU 32-63)
    - Shortcut logic: high-priority IRQs (bits 10-20) bypass bank summary
    - USB is Bank 1 IRQ 9, shortcut to bit 11
    - ENABLE_1/DISABLE_1 for Bank 1 interrupts
- **HAL properly refactored:**
  - `kernel/hal/qemu/irq.c` - GIC-400 driver with handle_irq()
  - `kernel/hal/pizero2w/irq.c` - BCM2836+BCM2835 driver with handle_irq()
  - `kernel/irq.c` - Thin wrappers + shared exception handlers
  - No more #ifdef spaghetti - clean platform separation
- **ARM Generic Timer working on Pi:**
  - Same timer as QEMU (CNTPNSIRQ)
  - Routes through ARM Local Controller bit 1
  - 100ms interval, prints "Interrupt!" every second (10 ticks)
- **GPIO driver added** (`kernel/hal/pizero2w/gpio.c`):
  - GPIO 47 (ACT LED) configured as output
  - LED toggle on each interrupt (didn't visibly blink but code is correct)
- **Clean-room implementation:**
  - BCM2835 IC spec generated from Linux irq-bcm2835.c
  - ARM Local Controller spec generated from Linux irq-bcm2836.c
  - No GPL code copied - just hardware register documentation
- **What this enables:**
  - USB interrupts for HID keyboard (IRQ 17 = Bank 1 bit 9)
  - Proper interrupt-driven USB driver (no polling!)
  - Foundation for DMA-based USB transfers
- **Files created:**
  - `kernel/hal/pizero2w/irq.c` - Pi interrupt controller driver
  - `kernel/hal/pizero2w/gpio.c` - Pi GPIO driver
  - `kernel/hal/qemu/irq.c` - QEMU GIC driver (moved from kernel/irq.c)
- **Achievement**: Interrupts fire on real Pi hardware! Timer ticks, handler runs!

### Session 42
- **QEMU raspi3b debugging infrastructure for USB**
- **Problem:** USB keyboard works partially on real Pi (enumerates, finds keyboard, no input) but very hard to debug with only serial output
- **Solution:** Run Pi build in QEMU raspi3b with same DWC2 controller for easier debugging
- **Pi serial driver rewritten:**
  - Switched from Mini UART (0x3F215000) to PL011 (0x3F201000)
  - PL011 works on both real Pi (with serial cable) and QEMU raspi3b
  - QEMU's `-serial stdio` connects to PL011
- **Printf output control:**
  - Added `PRINTF_UART` compile flag
  - `PRINTF=uart` (default) sends printf to UART
  - `PRINTF=screen` sends printf to framebuffer console
  - Same default for both targets (not target-dependent)
- **Fixed QEMU hang during USB init:**
  - Original `usleep()`/`msleep()` used nop loops calibrated for 1GHz Pi
  - M2 laptop runs QEMU much faster - "100ms" delay was microseconds
  - QEMU couldn't keep up with rapid mailbox/register polling
  - **Fix:** Use ARM generic timer (`cntpct_el0`, `cntfrq_el0`) for real delays
- **Added `make run-pi` target:**
  - Builds with `TARGET=pi`
  - Runs in QEMU raspi3b with USB keyboard attached
  - One command to test USB changes
- **Disabled noisy interrupt logging** (was printing every 100ms)
- **USB debugging findings:**
  - QEMU raspi3b boots, initializes USB, detects device as Full Speed
  - Fails at first GET_DESCRIPTOR with: `usb_generic_handle_packet: ctrl buffer too small (43551 > 4096)`
  - 43551 = 0xAA1F = garbage in wLength field
  - QEMU is stricter than real Pi - catches malformed packets that real hardware tolerates
  - Real Pi: enumeration succeeds but keyboard input doesn't work
  - Different failure points, possibly related root cause
- **Files modified:**
  - `kernel/hal/pizero2w/serial.c` - PL011 instead of Mini UART
  - `kernel/hal/pizero2w/usb.c` - Timer-based delays, debug prints
  - `kernel/hal/pizero2w/irq.c` - Disabled interrupt spam
  - `kernel/printf.c` - PRINTF_UART flag support
  - `Makefile` - PRINTF option, `run-pi` target
- **Achievement**: Can now debug USB driver in QEMU with full serial output!

### Session 43
- **USB KEYBOARD WORKING IN QEMU!**
- **Root cause found:** QEMU's DWC2 emulation only supports DMA mode, not slave mode
  - Slave mode = CPU manually reads/writes FIFOs
  - DMA mode = Controller reads/writes memory directly
  - Our original driver used slave mode, QEMU ignored FIFO operations
- **DMA mode implementation:**
  - Enabled `GAHBCFG_DMA_EN` (bit 5) in AHB configuration
  - Added 32-byte aligned DMA buffers for transfers
  - Set `HCDMA(ch)` register to bus address instead of writing to `FIFO(ch)`
  - Removed all FIFO read/write code
  - Simplified wait logic (no more RXFLVL polling)
- **USB hub support added:**
  - QEMU raspi3b has virtual 8-port root hub
  - Keyboard connected behind hub, not directly to root
  - Added hub descriptor fetching
  - Added port power on, status check, reset sequence
  - Recursive enumeration through hub ports
  - Found keyboard on hub port 1, address 2
- **HID keyboard interrupt transfers:**
  - Implemented interrupt IN transfers using DMA
  - 8-byte HID boot keyboard reports working
  - Data toggle (DATA0/DATA1) alternation between transfers
  - NAK handling (no data available, not an error)
- **HID report parsing:**
  - Created `kernel/hal/pizero2w/keyboard.c`
  - USB HID scancodes to ASCII conversion
  - Modifier key support (Shift for uppercase/symbols)
  - Ctrl+key combinations (Ctrl+A = 1, etc.)
  - Arrow keys and special keys
- **Keyboard integration:**
  - Modified `kernel/keyboard.c` to fall back to HAL when no virtio keyboard
  - `keyboard_getc()` calls `hal_keyboard_getc()` when `kbd_base == NULL`
  - Works seamlessly - shell accepts keyboard input!
- **Debug output improvements:**
  - Added debug levels (0=errors, 1=key events, 2=verbose)
  - `usb_info()` for important status, `usb_debug()` for verbose
  - Much cleaner output for real hardware testing
- **Real Pi status:**
  - USB enumeration works (hub found, keyboard found)
  - Interrupt transfers return 0 bytes (NAK)
  - Likely cache coherency issue - QEMU doesn't emulate caches
  - DMA buffer might need uncached memory or cache flush/invalidate
  - Deferred to future session
- **Files changed:**
  - `kernel/hal/pizero2w/usb.c` - DMA mode, hub support, interrupt transfers
  - `kernel/hal/pizero2w/keyboard.c` - New file, USB HID to ASCII
  - `kernel/keyboard.c` - HAL fallback for Pi
- **Achievement**: USB keyboard fully working in QEMU raspi3b! Type in shell!

### Session 44
- **Framebuffer performance optimization**
- **Problem:** Pi Zero 2W was painfully slow - every pixel operation was byte-by-byte
- **Root cause:** `memcpy`, `memmove`, `memset` all used byte loops
  - Console scroll = 6+ million byte operations per scroll
  - Screen clear = 3+ million byte operations
- **Optimizations implemented:**
  - `memcpy` - 64-bit copies when 8-byte aligned (8x faster)
  - `memset` - 64-bit stores when aligned (8x faster)
  - `memmove` - uses fast memcpy path when no overlap, 64-bit backward copy
  - Added `memset32` - fills with 32-bit pattern using 64-bit stores (2 pixels/op)
  - `fb_clear` - now uses `memset32`
  - `fb_fill_rect` - uses `memset32` per row instead of pixel loops
  - `fb_draw_char` - unrolled 8-pixel rows, removed per-pixel bounds checks
  - `scroll_up` - uses `memmove` + `memset32`
- **Result:** ~8-12x faster framebuffer operations on Pi
- **Files changed:**
  - `kernel/string.c` - optimized memcpy/memmove/memset, added memset32
  - `kernel/string.h` - added memset32 declaration
  - `kernel/fb.c` - use memset32, optimized fb_draw_char
  - `kernel/console.c` - use memmove/memset32 for scroll
- **TODO:** Apply same optimizations to userspace `gfx.h` for desktop/GUI apps
Session 44: USB Keyboard Working on Real Pi Hardware!

  Goal: Fix USB keyboard on Raspberry Pi Zero 2W (worked in QEMU, not on real hardware)

  Issues Fixed:

  1. Port Power Being Disabled (HPRT0 bug)
    - Port interrupt handler was writing only W1C bits, clearing PRTPWR
    - Fix: Preserve RW bits when clearing W1C status bits, mask out PRTENA
  2. Interrupt Storm / Too Fast Polling
    - SOF interrupt firing 1000x/sec was overwhelming the Pi
    - Removed SOF-based polling, switched to timer-tick based (every 10ms)
    - Reduced channel interrupt mask to just CHHLTD + errors
  3. CPU Cache Coherency (THE BIG ONE)
    - QEMU doesn't emulate caches, so DMA "just works"
    - Real Pi: CPU writes sit in L1 cache, DMA reads stale RAM = garbage
    - Added clean_data_cache_range() before OUT/SETUP transfers
    - Added invalidate_data_cache_range() before/after IN transfers
    - Increased DMA buffer alignment from 32 to 64 bytes (Cortex-A53 cache line)
  4. Missing SET_PROTOCOL
    - HID keyboards default to Report Protocol (complex reports)
    - Added SET_PROTOCOL(0) to switch to Boot Protocol (simple 8-byte reports)
    - Added SET_IDLE(0) to only report on key state changes

  Key Learnings:

  - Cache coherency is critical for DMA on real ARM hardware
  - dc cvac cleans cache to point of coherency (flush to RAM)
  - dc ivac invalidates cache (force re-read from RAM)
  - Always clean before DMA reads from buffer, invalidate before CPU reads DMA results

  Files Modified:

  - kernel/hal/pizero2w/usb.c - Cache maintenance, SET_PROTOCOL, interrupt handling
  - kernel/hal/pizero2w/irq.c - Timer-based keyboard polling
  - kernel/hal/hal.h - Added hal_usb_keyboard_tick()

  USB keyboard now works on real Raspberry Pi Zero 2W hardware!

### Session 45
- **USB Driver Refactor** - Split 2256-line monolithic driver into maintainable modules
- **Problem:** USB driver was too large and had reliability issues
  - Printf calls in ISR causing timing problems
  - Single keyboard buffer could drop fast keypresses
  - No recovery from stuck transfers
- **Refactored into `kernel/hal/pizero2w/usb/` directory:**
  - `dwc2_regs.h` (~200 lines) - Register definitions
  - `usb_types.h` (~180 lines) - USB descriptors and structs
  - `dwc2_core.c/h` (~400 lines) - PHY, reset, cache ops, mailbox, port control
  - `usb_transfer.c/h` (~290 lines) - Control transfers, DMA handling
  - `usb_enum.c/h` (~380 lines) - Device enumeration, hub support
  - `usb_hid.c/h` (~340 lines) - Keyboard ISR, polling, ring buffer
  - `usb.c` (~200 lines) - Init wrapper
- **New reliability features:**
  1. **Ring buffer** (16 reports) - Fast typing won't drop keys
  2. **Debug counters** - No printf in ISR, safe atomic counters
  3. **Watchdog** - Recovers stuck transfers after 50ms timeout
  4. **`usbstats` command** - Shows IRQ/transfer/error statistics
- **Files changed:**
  - `kernel/hal/pizero2w/usb/` - New directory with split USB driver
  - `kernel/shell.c` - Added `usbstats` recovery command
  - `Makefile` - Added USB subdirectory compilation rules
- **Stats output:** `[USB-STATS] IRQ=X KBD=X data=X NAK=X err=X restart=X port=X watchdog=X`

### Session 46
- **SD Card & FAT32 Performance Optimization**
- **Problem:** SD card operations on Pi were painfully slow
  - Single-sector commands: Each 512-byte read = separate CMD17 command
  - No FAT caching: Every cluster lookup read a sector from disk
  - Low clock speed: Running at 25MHz instead of 50MHz
- **Optimizations implemented:**
  1. **Multi-block SD commands** (`kernel/hal/pizero2w/emmc.c`)
     - Added `read_data_blocks()` / `write_data_blocks()` for multi-sector transfers
     - CMD18 (READ_MULTIPLE_BLOCK) with auto-CMD12 for reads
     - CMD25 (WRITE_MULTIPLE_BLOCK) with auto-CMD12 for writes
     - Single-sector still uses CMD17/CMD24 for compatibility
  2. **High Speed mode** (`kernel/hal/pizero2w/emmc.c`)
     - Added CMD6 (SWITCH_FUNC) to enable High Speed mode after init
     - Clock increased from 25MHz to 50MHz (2x improvement)
  3. **FAT sector cache** (`kernel/fat32.c`)
     - 8-entry LRU cache for FAT table sectors
     - `fat_read_sector_cached()` - returns cached data or reads from disk
     - `fat_next_cluster()` now uses cached reads
     - `fat_cache_invalidate()` called on FAT writes to maintain coherency
- **Performance improvement:**
  - Reading 8 sectors: 8 commands → 1 command
  - FAT chain traversal (10 clusters): 10 disk reads → 1-2 reads (cache hits)
  - Raw transfer speed: 2x (50MHz vs 25MHz)
- **Files changed:**
  - `kernel/hal/pizero2w/emmc.c` - Multi-block commands, High Speed mode
  - `kernel/fat32.c` - FAT sector cache with LRU eviction
- **Result:** File operations dramatically faster on real Pi hardware

  ### Session 46
  - **MASSIVE COREUTILS EXPANSION - 27 new commands!**
  - **File Operations (11):**
    - `cp` - copy files/directories with `-r` recursive support
    - `mv` - move/rename (uses rename for same-dir, copy+delete for cross-dir)
    - `head` / `tail` - first/last N lines (`-n` flag)
    - `wc` - word/line/char count (`-lwc` flags)
    - `grep` - simple substring search (`-i` case insensitive, `-n` line numbers, `-v` invert)
    - `find` - find files by name pattern (`-name`, `-type f|d`)
    - `stat` - file size and type
    - `du` - disk usage with `-h` human-readable, `-s` summary
    - `df` - filesystem free space
    - `hexdump` - hex dump with `-C` canonical format
  - **System Info (7):**
    - `ps` - process list showing PID, state, name
    - `kill` - terminate process by PID (real implementation!)
    - `free` - memory usage (`-h`, `-m`, `-k` flags)
    - `uname` - system info (`-a`, `-s`, `-n`, `-r`, `-m`)
    - `hostname` - print hostname
    - `lscpu` - CPU info (model, frequency, cores, RAM)
    - `lsusb` - USB device list (Pi shows real devices, QEMU shows none)
  - **Misc (9):**
    - `sleep` - sleep N seconds
    - `seq` - print number sequences
    - `which` - find command in /bin
    - `whoami` - print current user ("user")
    - `yes` - repeat string forever
    - `clear` - clear screen
    - `basename` / `dirname` - path manipulation
  - **Kernel changes for kill/lscpu/lsusb:**
    - Added `process_kill(int pid)` to kernel/process.c
    - Added HAL functions: `hal_get_cpu_name()`, `hal_get_cpu_freq_mhz()`, `hal_get_cpu_cores()`
    - Added HAL functions: `hal_usb_get_device_count()`, `hal_usb_get_device_info()`
    - QEMU: returns Cortex-A72 @ 1500MHz, 1 core, no USB devices
    - Pi: returns Cortex-A53 @ 1000MHz, 4 cores, real USB device list
  - **Files created:** 27 new files in user/bin/
  - **Files modified:** Makefile, kernel/process.c, kernel/process.h, kernel/kapi.c, kernel/kapi.h, user/lib/vibe.h, kernel/hal/hal.h, kernel/hal/qemu/platform.c, kernel/hal/pizero2w/platform.c
  - **Achievement**: VibeOS now has a proper Unix-like coreutils suite!

### Session 47
- **Preemptive Multitasking Implementation**
- **Goal:** Switch from cooperative (apps must call yield()) to preemptive (timer forces context switches)
- **Initial approach (WRONG):** Make yield() a no-op, rely entirely on timer preemption
- **Problem discovered:** With 10 apps running, system crawled to a halt
  - Each process got 50ms timeslice
  - Full round-robin = 10 × 50ms = 500ms per cycle
  - Apps spinning in event loops burned their full slice doing nothing
  - With cooperative, apps yielded immediately when waiting for input
- **Key insight:** Real OSes (Linux) use BOTH mechanisms:
  1. Voluntary yield - apps yield when waiting (fast path, primary)
  2. Preemptive backup - timer forces switch for CPU hogs (safety net)
- **Implementation details:**
  - Expanded `cpu_context_t` to save ALL registers (x0-x30, sp, pc, pstate, FPU)
  - Modified `vectors.S` IRQ handler to save/restore full context
  - Added `CONTEXT_OFFSET` (0x50) - cpu_context_t offset within process_t
  - Changed `context.S` to use `eret` instead of `ret` (restores PSTATE/IRQ state)
  - Timer fires at 100Hz (10ms) for audio/responsiveness
  - Preemption check every 5 ticks (50ms timeslice)
  - `process_schedule_from_irq()` called from timer - updates current_process
- **Bugs fixed along the way:**
  - Kernel panic: process slot with invalid context (sp=0) - added safety check
  - Process exits immediately: pstate missing EL1h mode bits - save as `DAIF | 0x5`
  - IRQs disabled after voluntary switch: `ret` doesn't restore PSTATE - use `eret`
  - Context corruption: vectors.S writing to offset 0 instead of 0x50
- **Final fix:** Restored yield() to actually switch processes
  - `kapi.yield = process_yield` (not noop)
  - Apps that yield() switch immediately
  - Apps that don't yield get preempted after 50ms
- **Files modified:**
  - `kernel/process.c` - process_schedule_from_irq(), kernel_context global
  - `kernel/process.h` - expanded cpu_context_t
  - `kernel/vectors.S` - full context save/restore, CONTEXT_OFFSET
  - `kernel/context.S` - eret instead of ret, proper pstate save
  - `kernel/hal/qemu/irq.c` - timer calls scheduler every 5 ticks
  - `kernel/hal/pizero2w/irq.c` - same for Pi
  - `kernel/kapi.c` - yield = process_yield (not noop)
- **Result:** Preemptive multitasking with cooperative fast path - best of both worlds!

### Session 48
- **Display Performance Optimizations for Raspberry Pi**
- **Problem:** vim and other text apps were painfully slow on Pi, but snake/tetris were fine
- **Root cause analysis:**
  - Snake/Tetris: only update ~200 changed pixels per frame
  - Vim: redraws entire screen (~480,000 pixels) on every keystroke
  - Each `putc(' ')` for clearing = 128 pixel writes
- **Optimizations implemented:**
  1. **Fast console clearing functions** (`kernel/console.c`)
     - `console_clear_to_eol()` - uses `fb_fill_rect` instead of putc loop
     - `console_clear_region(row, col, w, h)` - fast rectangular clear
     - Exposed via kapi for userspace programs
  2. **Pi GPU hardware scroll** (`kernel/hal/pizero2w/fb.c`)
     - Virtual framebuffer 2x physical height (1200 vs 600 pixels)
     - `hal_fb_set_scroll_offset(y)` - instant GPU register update
     - Circular buffer approach: scroll ~37 lines before needing to copy
     - Wrap copies visible portion back to top, resets offset
  3. **Vim optimizations** (`user/bin/vim.c`)
     - `redraw_screen()` clears all rows with one `clear_region` call
     - `draw_line()` uses `clear_line()` before drawing text
     - Status/command line use fast clear instead of putc loops
     - Fixed scroll bug: trailing space on status line triggered unwanted scroll
- **Hardware scroll bugs fixed:**
  - "Sideways scrolling" - memmove used `fb_base + scroll_offset` instead of `fb_base + scroll_offset * fb_width`
  - The offset is Y pixels, must multiply by width to get linear pixel index
- **Framebuffer bounds fix** (`kernel/fb.c`)
  - Added `fb_buffer_height` for virtual buffer size
  - Drawing functions now clip to buffer height, not just visible height
  - Allows drawing in scroll area above/below visible region
- **Files modified:**
  - `kernel/hal/hal.h` - added `hal_fb_set_scroll_offset()`, `hal_fb_get_virtual_height()`
  - `kernel/hal/pizero2w/fb.c` - 2x virtual height, hardware scroll via mailbox
  - `kernel/hal/qemu/fb.c` - stub returns -1 (no hardware scroll)
  - `kernel/fb.c` - fb_buffer_height for bounds checking
  - `kernel/console.c` - hardware scroll, fast clear functions
  - `kernel/console.h` - new function declarations
  - `kernel/kapi.c/h` - exposed clear functions
  - `user/lib/vibe.h` - kapi struct updated
  - `user/bin/vim.c` - use fast clear, fix scroll bugs
- **Performance improvement:**
  - Console scroll: memmove every line → GPU offset update (37x fewer copies)
  - Vim clear: ~100 putc per line → 1 fb_fill_rect call
  - Overall: text apps significantly faster on Pi

### Session 49
- **Kernel Ring Buffer and dmesg**
- **Goal:** Add a kernel logging system like Linux dmesg
- **Implementation:**
  1. **Kernel ring buffer** (`kernel/klog.c`, `kernel/klog.h`)
     - 64KB static circular buffer
     - `klog_putc()` writes one char, wraps around when full
     - `klog_read()` reads from any offset, handles wrap-around
     - `klog_size()` returns current log size
  2. **Printf integration** (`kernel/printf.c`)
     - Added `klog_putc(c)` to `printf_putchar()` - always logs
     - Happens in addition to UART/screen output, not instead of
     - No compile flags needed, fully automatic
  3. **Early initialization** (`kernel/kernel.c`)
     - `klog_init()` called before `memory_init()`
     - Uses static buffer, no malloc needed
     - Captures all boot messages from first printf onward
  4. **kapi exposure** (`kernel/kapi.c`, `kernel/kapi.h`, `user/lib/vibe.h`)
     - `klog_read(buf, offset, size)` - read log data
     - `klog_size()` - get total logged bytes
  5. **dmesg program** (`user/bin/dmesg.c`)
     - Interactive scrollable viewer (default)
     - `-n` flag for non-interactive dump
     - Controls: j/k arrows scroll, g/G start/end, u/d page, q quit
     - Status bar shows position (e.g., "45-68/120")
- **Files created:**
  - `kernel/klog.h` - ring buffer header
  - `kernel/klog.c` - ring buffer implementation
  - `user/bin/dmesg.c` - log viewer program
- **Files modified:**
  - `kernel/printf.c` - added klog_putc() call
  - `kernel/kernel.c` - klog_init() early in boot
  - `kernel/kapi.c/h` - exposed klog functions
  - `user/lib/vibe.h` - kapi struct updated
  - `Makefile` - added dmesg to USER_PROGS
