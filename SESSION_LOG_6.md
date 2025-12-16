# VibeOS Session Log 6 - MicroPython Port

## Session 56: MicroPython Interpreter Port

**Goal**: Port MicroPython to VibeOS as a userspace program with access to kernel API.

### Why MicroPython?
- Python REPL running on bare metal
- Scripting language for VibeOS applications
- Interactive development/testing
- MIT licensed, minimal dependencies

### Initial Approach (Abandoned)
- Tried putting MicroPython in `user/bin/micropython/`
- MicroPython's build system requires qstring generation step
- Copying sources and building with our Makefile didn't work
- Qstrings are auto-generated from source scanning

### Working Approach
- Created proper port in `micropython/ports/vibeos/`
- Uses MicroPython's own build system (py.mk, mkrules.mk)
- Build system handles qstring generation automatically

### Port Files Created
1. **`mpconfigport.h`** - Configuration:
   - `MICROPY_CONFIG_ROM_LEVEL_MINIMUM` - Minimal feature set
   - No floats (avoids libgcc soft-float dependencies)
   - No external modules (json, re, random disabled)
   - Minimal sys module (needed for REPL prompts)
   - 2MB heap for garbage collector
   - `MICROPY_HAL_HAS_VT100 = 0` - VibeOS console doesn't support escape codes

2. **`mphalport.c`** - Hardware Abstraction Layer:
   - `mp_hal_stdin_rx_chr()` - Keyboard input via kapi
   - `mp_hal_stdout_tx_strn()` - Console output via kapi
   - `mp_hal_ticks_ms()` - Uptime from kernel
   - `mp_hal_delay_ms()` - Sleep via kapi
   - Key translation: `'\n'` → `'\r'` for Enter (readline expects CR)
   - Special key conversion: Arrow keys → VT100 escape sequences

3. **`modvibe.c`** - Python `vibe` Module:
   - Console: `clear()`, `puts()`, `set_color(fg, bg)`
   - Input: `has_key()`, `getc()`
   - Timing: `sleep_ms()`, `uptime_ms()`, `yield()`
   - Graphics: `put_pixel()`, `fill_rect()`, `draw_string()`, `screen_size()`
   - Mouse: `mouse_pos()`, `mouse_buttons()`
   - Memory: `mem_free()`, `mem_used()`
   - Color constants: `BLACK`, `WHITE`, `RED`, `GREEN`, `BLUE`, `YELLOW`, `CYAN`, `MAGENTA`

4. **`main.c`** - Entry point:
   - Receives kapi pointer from kernel
   - Initializes MicroPython runtime and GC
   - Runs `pyexec_friendly_repl()`

5. **`setjmp.S`** - AArch64 setjmp/longjmp:
   - Required for GC register scanning
   - Saves/restores x19-x30, sp

6. **`stubs.c`** - Missing function stubs:
   - `strchr()` - Non-inline version for address-taking
   - `mp_sched_keyboard_interrupt()` - Ctrl+C handler (no-op)
   - `mp_hal_move_cursor_back()` - Backspace handling (uses `\b`)
   - `mp_hal_erase_line_from_cursor()` - Line clear (spaces + backspace)

### Kernel libc Extensions
Added headers for freestanding MicroPython build:
- `setjmp.h` - jmp_buf typedef, setjmp/longjmp declarations
- `stddef.h` - size_t, ssize_t, ptrdiff_t, NULL, offsetof
- `stdint.h` - int8_t through int64_t, limits
- `string.h` - Inline implementations of memcpy, strlen, strchr, etc.
- `errno.h` - Full set of POSIX error codes
- `stdio.h` - Added SEEK_SET/CUR/END
- `math.h` - Added NAN, INFINITY, isnan, isinf
- `assert.h` - Added NDEBUG guard

### Build Issues Solved
1. **NDEBUG redefinition** - MicroPython's `-DNDEBUG` conflicted with assert.h
2. **Missing math macros** - Added NAN, INFINITY, HUGE_VAL to math.h
3. **Missing ssize_t** - Added to stddef.h
4. **strchr implicit declaration** - Added `-include string.h` to CFLAGS
5. **Missing errno codes** - Expanded errno.h
6. **extmod dependencies** - Created stub headers (virtpin.h, vfs.h, modplatform.h)
7. **VT100 escape codes** - Disabled, implemented simple backspace handling

### MicroPython Source Cleanup
Trimmed from ~50MB to ~3.5MB:
- **Kept**: `py/`, `shared/`, `ports/vibeos/`, `LICENSE`
- **Deleted**: All other ports, drivers, lib, extmod, tools, docs, tests, examples
- Created minimal `extmod/` stubs for headers still referenced by py/

### Python Demo Scripts (`python/`)
1. **`hello.py`** - Hello world with colors
2. **`graphics.py`** - Draws colored rectangles
3. **`mouse.py`** - Paint program with mouse
4. **`bounce.py`** - Bouncing ball animation at 60fps
5. **`sysinfo.py`** - System info display

### Binary Size
- Text: 130KB
- Data: 12KB
- BSS: 2MB (GC heap)

### What Works
- Full Python REPL with line editing
- `import vibe` module
- All vibe functions (console, graphics, input, timing)
- `print()` (uses our HAL)
- Backspace, Enter, arrow keys
- Arbitrary precision integers (`2 ** 100`)

### What Doesn't Work Yet
- **Running .py files from disk** - Needs `mp_lexer_new_from_file()` implemented in main.c
  - Currently only REPL mode works
  - Need to either:
    1. Implement file lexer using kapi file I/O functions
    2. Add `vibe.read_file()` helper and use `exec(vibe.read_file("/python/hello.py"))`
- **Floats** - Disabled to avoid libgcc soft-float dependencies
- **External modules** - json, re, random disabled (could be enabled if needed)

### Missing kapi Bindings in vibe Module
The vibe module only exposes a small subset of kapi. Missing bindings:

**File I/O** (HIGH PRIORITY for running .py files):
- `open()`, `read()`, `write()`, `create()`, `mkdir()`, `delete()`, `rename()`
- `readdir()`, `is_dir()`, `file_size()`
- `set_cwd()`, `get_cwd()`

**Console Extended**:
- `putc()`, `uart_puts()`, `set_cursor()`, `set_cursor_enabled()`
- `print_int()`, `print_hex()`, `clear_to_eol()`, `clear_region()`
- `console_rows()`, `console_cols()`

**Memory Allocation**:
- `malloc()`, `free()` - Could expose for large buffers

**Process Management**:
- `exit()`, `exec()`, `spawn()`, `kill_process()`
- `get_process_count()`, `get_process_info()`

**Time & RTC**:
- `get_timestamp()`, `get_datetime()` - For date/time scripts

**Sound**:
- `sound_play_wav()`, `sound_play_pcm()`, `sound_stop()`, `sound_pause()`, `sound_resume()`

**Networking** (for HTTP/fetch scripts):
- `net_ping()`, `net_get_ip()`, `dns_resolve()`
- `tcp_connect()`, `tcp_send()`, `tcp_recv()`, `tcp_close()`
- `tls_connect()`, `tls_send()`, `tls_recv()`, `tls_close()`

**System Info**:
- `get_ram_total()`, `get_disk_total()`, `get_disk_free()`
- `get_cpu_name()`, `get_cpu_freq_mhz()`, `get_cpu_cores()`
- `usb_device_count()`, `usb_device_info()`
- `klog_read()`, `klog_size()` - For dmesg viewer

**Hardware** (Pi-specific):
- `led_on()`, `led_off()`, `led_toggle()`, `led_status()`
- `fb_has_hw_double_buffer()`, `fb_flip()`, `fb_get_backbuffer()`
- `dma_available()`, `dma_copy()`, `dma_fill()`

**Advanced Graphics**:
- `ttf_get_glyph()`, `ttf_get_advance()`, `ttf_get_kerning()` - TrueType fonts
- `font_data` - Direct font access
- `fb_base` - Direct framebuffer access
- `window_*` functions - For desktop apps

**Not Needed**:
- `stdio_*` hooks - Internal to terminal emulator
- Debug memory functions - For debugging only

### Files Created
- `micropython/ports/vibeos/*` - Port implementation
- `micropython/extmod/*.h` - Stub headers
- `kernel/libc/setjmp.h`, `stddef.h`, `stdint.h` - New headers
- `python/*.py` - Demo scripts

### Files Modified
- `Makefile` - MicroPython build integration, python/ install
- `kernel/libc/*.h` - Extended for freestanding build

### Licensing
- MicroPython core is MIT licensed
- All non-MIT code (drivers, lib, other ports) was deleted
- Only need to keep `micropython/LICENSE` file

### Lessons Learned
1. MicroPython's build system is complex but handles qstring generation automatically
2. Creating a proper port in `ports/` is easier than fighting the build system
3. VT100 escape codes assumed by readline - need stubs if terminal doesn't support them
4. Key codes differ: VibeOS uses `'\n'` for Enter, readline expects `'\r'`
5. Trimming MicroPython source is safe - just keep py/, shared/, and your port

### Future Work
Priority order for expanding Python capabilities:

1. **File execution** - Implement `mp_lexer_new_from_file()` so you can run `/python/hello.py`
   - Add file I/O bindings to vibe module first
   - Or add simple `vibe.read_file()` helper

2. **Enable modules** - Turn on json, re if needed
   - Would need to verify no problematic dependencies
   - Might increase binary size significantly

3. **Add networking bindings** - tcp/dns for HTTP scripts
   - Could write a simple HTTP client in Python
   - Fetch and parse web data

4. **Add sound bindings** - Play WAV/PCM from Python
   - Music player scripts
   - Sound effects for games

5. **Floats** - If needed for calculations
   - Requires libgcc soft-float library
   - Would increase binary size

6. **Process control** - spawn(), exec() for launching programs from Python
   - Python as a shell scripting language

7. **TrueType fonts** - Render text with TTF from Python
   - GUI applications with nice fonts

---

## Session 57: MicroPython Memory Layout Bug

**Problem**: MicroPython (and eventually all processes) crashed on exit with corrupted `kernel_context`.

### Symptoms
- `sys.exit()` crashed and rebooted the system
- Ctrl+D also crashed
- Debug output showed `kernel_context` with suspicious values:
  - `pc=0x12ce0` (small address)
  - `sp=0x5efffe80` (near kernel stack)

### Investigation
1. Added debug prints to track `kernel_context` state at process exit
2. Found MicroPython was loading at `0x5ef20000-0x5f143afc` (ending ABOVE the kernel stack at `0x5f000000`)
3. Root cause: **heap consumed ALL available RAM**, pushing program load area to overlap with kernel stack

### Memory Layout Bug
The kernel heap was calculated to extend nearly to the stack:
```c
heap_max = KERNEL_STACK_TOP - STACK_BUFFER;  // Only 1MB buffer
```

Programs load at `ALIGN_64K(heap_end)`, which was ~`0x5ef00000`. MicroPython's 2.2MB binary (130KB code + 2MB BSS heap) extended past the kernel stack at `0x5f000000`.

### Fix
Reserved 64MB for program area between heap and stack:
```c
uint64_t program_reserve = 64 * 1024 * 1024;  // 64MB for programs
uint64_t heap_max = KERNEL_STACK_TOP - STACK_BUFFER - program_reserve;
```

New layout:
- Heap: `0x402d2430 - 0x5af00000` (~430MB)
- Program area: `0x5af00000 - 0x5f000000` (~80MB)
- Stack: `0x5f000000`

### False Alarm: "Corrupted" Context
After fixing memory layout, still saw "corruption" errors. Investigation revealed:
- Kernel CODE lives at `0x0` (flash), not `0x40000000` (RAM)
- `pc=0x12cf0` is INSIDE `process_schedule` (at `0x129c0`) - **VALID**
- `sp=0x5efffe80` is kernel stack minus 384 bytes - **VALID**

The sanity check was wrong:
```c
// WRONG: kernel code is in flash at 0x0, not RAM
if (kernel_context.pc < 0x40000000) { ... }

// FIXED: only check for NULL
if (kernel_context.pc == 0 || kernel_context.sp == 0) { ... }
```

### Files Modified
- `kernel/memory.c` - Reserve program area, add heap debug print
- `kernel/process.c` - Fix sanity check for flash-based kernel code

### Improvements Made Along the Way
- `micropython/ports/vibeos/main.c`:
  - Aligned heap to 16 bytes: `__attribute__((aligned(16)))`
  - Better stack tracking for GC using inline asm
  - Sanity check in `gc_collect()` to prevent invalid memory scans

### Lessons Learned
1. **Memory layout matters** - Programs need reserved space, not just leftovers after heap
2. **Know your memory map** - Kernel code at 0x0 (flash) vs data at 0x40000000 (RAM)
3. **Debug prints first, cleanup later** - Don't remove diagnostics until fix is verified

---

## Session 58: MicroPython Script Files + Full API Port

**Goal**: Run .py files from disk and expose the entire kernel API to Python.

### Script File Execution

Implemented `micropython script.py` support:

1. **`mp_lexer_new_from_file()`** - Opens files from VFS, reads contents, creates lexer
2. **`mp_import_stat()`** - Checks if paths exist (file vs directory)
3. **CRLF handling** - Strip `\r` characters (FAT32 files from macOS have CRLF line endings)

**Bug Found**: `vibe.yield()` caused syntax error!
- `yield` is a Python keyword (for generators)
- Renamed to `vibe.sched_yield()`

### Full API Port

Ported the ENTIRE kapi to Python bindings in `modvibe.c`:

**Console I/O** (7 functions):
- `putc`, `puts`, `clear`, `set_color`, `set_cursor`, `set_cursor_enabled`, `console_size`

**Filesystem** (12 functions):
- `open`, `read`, `write`, `file_size`, `is_dir`, `create`, `mkdir`, `delete`, `rename`, `listdir`, `getcwd`, `chdir`

**Process** (5 functions):
- `exit`, `exec`, `spawn`, `kill`, `ps`

**Graphics** (5 functions):
- `put_pixel`, `fill_rect`, `draw_char`, `draw_string`, `screen_size`

**Mouse** (2 functions):
- `mouse_pos`, `mouse_buttons`

**Windows** (6 functions):
- `window_create`, `window_destroy`, `window_poll`, `window_invalidate`, `window_set_title`, `window_size`

**Sound** (5 functions):
- `sound_play`, `sound_stop`, `sound_pause`, `sound_resume`, `sound_is_playing`

**Networking** (11 functions):
- `dns_resolve`, `ping`, `get_ip`
- `tcp_connect`, `tcp_send`, `tcp_recv`, `tcp_close`
- `tls_connect`, `tls_send`, `tls_recv`, `tls_close`

**System Info** (6 functions):
- `mem_free`, `mem_used`, `ram_total`, `disk_total`, `disk_free`, `cpu_info`

**Other** (4 functions):
- `usb_devices`, `led_on`, `led_off`, `led_toggle`

**RTC** (2 functions):
- `timestamp`, `datetime`

**Constants**:
- Colors: `BLACK`, `WHITE`, `RED`, `GREEN`, `BLUE`, `YELLOW`, `CYAN`, `MAGENTA`, `AMBER`
- Window events: `WIN_EVENT_NONE`, `WIN_EVENT_MOUSE_DOWN`, `WIN_EVENT_MOUSE_UP`, `WIN_EVENT_MOUSE_MOVE`, `WIN_EVENT_KEY`, `WIN_EVENT_CLOSE`, `WIN_EVENT_FOCUS`, `WIN_EVENT_UNFOCUS`
- Mouse buttons: `MOUSE_LEFT`, `MOUSE_RIGHT`, `MOUSE_MIDDLE`

### Test Suite

Created `python/test_api.py` - comprehensive test of all bindings:
- Tests each API category
- Color-coded output (green OK, red FAIL)
- Lists filesystem, processes, USB devices
- Checks DNS resolution
- All tests pass!

### Files Modified
- `micropython/ports/vibeos/main.c` - Script execution, CRLF handling
- `micropython/ports/vibeos/modvibe.c` - Full API (198 → 857 lines)
- `python/*.py` - Updated `yield` → `sched_yield`
- `Makefile` - Detect MicroPython source changes

### What This Enables

Python can now:
- Read/write files on disk
- List directories, navigate filesystem
- Spawn processes, view process list
- Create windows, handle events
- Play sounds
- Make HTTP/HTTPS requests (via TCP/TLS)
- Query system info (CPU, RAM, disk)
- Full graphics (pixels, rectangles, text)

**Next**: Rewrite the browser in Python! The full API makes this possible.

---

## Session 59: MicroPython Extended Modules + Crash Fix

**Goal**: Add standard library modules (json, random, re) and fix crash on Python builtins.

### Modules Added

Enabled standard library modules for building apps like browser in Python:

1. **json** - `json.loads()`, `json.dumps()` for parsing/serializing JSON
2. **random** - `random.randint()`, `random.random()` etc.
3. **re** - Regular expressions for HTML parsing
4. **heapq** - Priority queues
5. **math** - Full math module (sin, cos, sqrt, log, exp, etc.)

### Build System Changes

**Extended modules from MicroPython reference repo:**
- Copied `extmod/modjson.c`, `modrandom.c`, `modre.c`, `modheapq.c`
- Copied `lib/re1.5/` regex library (modre.c includes the .c files directly)
- Added to Makefile's `SRC_EXTMOD_C`

**Math library for floats:**
- Changed `MICROPY_FLOAT_IMPL` from NONE to DOUBLE
- Added libgcc linking for soft-float support
- Implemented full math library in `stubs.c`:
  - Basic: fabs, floor, ceil, trunc, fmod, copysign, nearbyint
  - Exponential: exp, expm1, log, log2, log10, pow, sqrt
  - Trig: sin, cos, tan, asin, acos, atan, atan2
  - Hyperbolic: sinh, cosh, tanh, asinh, acosh, atanh
  - Special: erf, erfc, tgamma, lgamma (stubs)
  - Float decomposition: modf, ldexp, frexp
- Added declarations to `kernel/libc/math.h`

**New libc header:**
- Created `kernel/libc/stdbool.h` for bool/true/false

### Critical Bug Fix: Stream-based stdout crash

**Symptom**: All Python builtins (print, json.loads, etc.) crashed with:
```
KERNEL PANIC: Data abort
ELR_EL1: 0x5af3f724 (in mp_stream_rw)
FAR_EL1: 0xd2a00600b900012a (garbage address)
```

**Root cause**:
- Enabled `MICROPY_PY_IO=1` for json module
- This implicitly enabled stream-based stdout
- Our stub `mp_sys_stdout_obj = MP_OBJ_NULL` was used as a stream
- `print()` called `mp_get_stream(NULL)` → crash

**Fix**:
```c
#define MICROPY_PY_SYS_STDFILES (0)  // Don't use stream-based stdout
```
With this disabled, print() uses `mp_hal_stdout_tx_strn()` (our HAL) instead.

### Other Fixes

- Added `mp_builtin_open_obj` stub (raises OSError - no file I/O support)
- Added `-DMP_FALLTHROUGH=` to CFLAGS for re1.5 library
- Changed optimization from `-Os` to `-O0` (safer for PIE binaries)

### Configuration Summary (mpconfigport.h)

```c
MICROPY_CONFIG_ROM_LEVEL = FULL_FEATURES
MICROPY_FLOAT_IMPL = DOUBLE
MICROPY_PY_JSON = 1
MICROPY_PY_RE = 1
MICROPY_PY_RANDOM = 1
MICROPY_PY_MATH = 1
MICROPY_PY_HEAPQ = 1
MICROPY_PY_STRUCT = 1
MICROPY_PY_COLLECTIONS = 1
MICROPY_PY_IO = 1
MICROPY_PY_SYS_STDFILES = 0  // Critical!
```

### Files Modified
- `micropython/ports/vibeos/Makefile` - extmod sources, -O0, libgcc, stdbool
- `micropython/ports/vibeos/mpconfigport.h` - enable modules, STDFILES=0
- `micropython/ports/vibeos/stubs.c` - math library, open() stub
- `kernel/libc/math.h` - math function declarations
- `kernel/libc/stdbool.h` - new file
- `kernel/elf.c` - debug output for relocations (can remove later)

### Files Added
- `micropython/extmod/modjson.c`
- `micropython/extmod/modrandom.c`
- `micropython/extmod/modre.c`
- `micropython/extmod/modheapq.c`
- `micropython/lib/re1.5/*` - regex library

### What Works Now
```python
import math
print(math.sin(math.pi / 2))  # 1.0

import json
data = json.loads('{"name": "VibeOS"}')
print(json.dumps(data))

import random
print(random.randint(1, 100))

import re
m = re.search(r'\d+', 'hello123')
print(m.group(0))  # "123"
```

### Binary Size
- Text: ~300KB (was 130KB before modules)
- BSS: 2MB (GC heap)
- Total: ~2.4MB

### Lessons Learned
1. **MICROPY_PY_IO enables stream-based stdout** - must explicitly disable STDFILES
2. **modre.c #includes its dependencies** - don't compile re1.5 separately
3. **Math functions need declarations** - add to math.h, not just stubs.c
4. **Relocation debugging is valuable** - the ELF loader processes 2321 relocations correctly

---

## Session 60: TCC (Tiny C Compiler) Port

**Goal**: Port TCC to VibeOS so users can compile C programs directly on the OS.

### Why TCC?
- Compile C code directly on VibeOS - no cross-compilation needed
- Fast compilation (single-pass)
- Tiny binary (~300KB)
- Self-contained - no external dependencies
- MIT licensed

### Port Structure

Created `tinycc/vibeos/` port directory:

1. **`config.h`** - TCC configuration:
   - `TCC_TARGET_ARM64` - AArch64 target
   - `CONFIG_TCC_STATIC` - No dlopen
   - `CONFIG_TCC_PIE` - Output PIE executables (required for VibeOS)
   - `CONFIG_TCCDIR="/lib/tcc"` - Installation path
   - Disabled: bounds checking, backtrace, semaphores

2. **`tcc_libc.c`** - C library implementation (~1000 lines):
   - File I/O: `fopen`, `fclose`, `fread`, `fwrite`, `fseek`, `ftell`
   - String: `strlen`, `strcpy`, `strcmp`, `strcat`, `strstr`, `memcpy`, `memset`
   - Memory: `malloc`, `free`, `realloc`, `calloc`
   - Printf family: `sprintf`, `snprintf`, `fprintf`
   - Conversion: `atoi`, `strtol`, `strtoul`
   - Character: `isalpha`, `isdigit`, `isspace`, `tolower`, `toupper`
   - Other: `qsort`, `abs`, `getenv`, `time`

3. **`tcc_libc.h`** - Header with all declarations

4. **`tcc_main.c`** - Entry point, receives kapi pointer

5. **`setjmp.S`** - AArch64 setjmp/longjmp implementation

6. **`Makefile`** - Builds TCC as PIE binary for VibeOS

7. **`include/`** - Standard C headers (stdio.h, stdlib.h, string.h, etc.)

8. **`libc.a`** / `libtcc1.a`** - Runtime libraries for compiled programs

9. **`crti.S` / `crtn.S`** - C runtime init/fini stubs

### Critical Bug #1: File Handle Sharing

**Symptom**: All files returned same handle, causing "undefined symbol main" for every file.

**Root cause**: `vfs_lookup()` returned pointer to static `temp_node` variable. All open files shared the same node.

**Fix**: Created new VFS API specifically for TCC's file I/O needs:
```c
// kernel/vfs.c
vfs_node_t *vfs_open_handle(const char *path);  // Allocates unique handle
void vfs_close_handle(vfs_node_t *node);        // Frees handle
```

Added `kapi->close` to kernel API for handle cleanup.

**Note**: This is a new API path used only by TCC. Existing apps continue using `kapi->open`/`vfs_lookup` which returns static nodes (works fine for read-only access).

### Critical Bug #2: FAT32 Write Truncation

**Symptom**: Compiled ELF was 149 bytes containing only ".text" string.

**Root cause**: `kapi->write` (FAT32) overwrites file from beginning each time. TCC calls `fwrite` many times - only the last write survived.

**Fix**: Buffer all writes in memory, flush entire buffer on `fclose`:
```c
// tcc_libc.c
size_t fwrite(const void *ptr, size_t size, size_t nmemb, FILE *f) {
    // Grow buffer as needed
    if (new_size > f->buf_size) {
        f->buf = realloc(f->buf, new_size + 4096);
    }
    memcpy(f->buf + f->pos, ptr, total);
    f->pos += total;
    return nmemb;
}

int fclose(FILE *f) {
    fflush(f);  // Write entire buffer to file
    kapi->close(f->handle);
    free(f->buf);
    free(f);
}
```

### Critical Bug #3: Non-PIE Output

**Symptom**: TCC produced EXEC type ELF with fixed vaddr=0x400000. VibeOS needs PIE.

**Fix**: Enable `CONFIG_TCC_PIE` in config.h and Makefile:
```c
#define CONFIG_TCC_PIE 1
```

This makes TCC set `output_type |= TCC_OUTPUT_DYN`, which sets base address to 0 (relocatable).

### Project Reorganization

Created `vibeos_root/` directory for disk content:
- `vibeos_root/beep.wav` - Sound test file
- `vibeos_root/duck.png` - Image test file
- `vibeos_root/hello.c` - TCC test program
- `vibeos_root/scripts/*.py` - MicroPython scripts

Removed from project root (moved to vibeos_root):
- `beep.wav`, `duck.*` images
- `fonts/Roboto/` (unused font files)
- `python/`, `scripts/` directories

### Build System Changes

Updated Makefile:
- Added TCC build target (`tinycc/vibeos/build/tcc`)
- Copy TCC and runtime to `/bin/tcc`, `/lib/tcc/`
- Copy `vibeos_root/` contents to disk image
- Detect TCC source changes for rebuild

### Files Created
- `tinycc/vibeos/*` - Complete TCC port
- `user/lib/crti.S`, `user/lib/crtn.S` - CRT stubs
- `vibeos_root/` - Disk content directory
- `vibeos_root/hello.c` - Test program

### Files Modified
- `kernel/vfs.c` - Added `vfs_open_handle()`, `vfs_close_handle()`
- `kernel/vfs.h` - Function declarations
- `kernel/kapi.c` - Added `kapi_close` wrapper
- `kernel/kapi.h` - Added `close` to kapi_t struct
- `user/lib/vibe.h` - Added `close` to kapi_t struct
- `kernel/process.c` - Added ELF validation debug output
- `Makefile` - TCC integration, vibeos_root copying
- `.gitignore` - Build artifacts, disk mount points

### Usage

```bash
# In VibeOS shell:
cd /home/user
tcc hello.c -o hello
./hello
```

### What Works
- Compiling simple C programs
- PIE output that loads at any address
- Access to kernel API via kapi pointer
- Standard C library functions

### What's Next
- Test more complex programs
- Add more libc functions as needed
- Potentially self-host TCC (compile TCC with TCC)

### Lessons Learned
1. **VFS handle sharing** - Static nodes work for read-only, but writers need unique handles
2. **FAT32 write semantics** - Must buffer writes and flush entire file at once
3. **PIE is essential** - VibeOS loads programs at dynamic addresses, fixed vaddr breaks everything
4. **Debug output to UART** - Console output may not work during compilation, use `kapi->uart_puts`
5. **TCC's architecture** - Clean separation of compiler core vs platform support
