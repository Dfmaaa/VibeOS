# VibeOS Optimization Audit Report

**Date**: December 2024
**Target**: Raspberry Pi Zero 2W performance
**Methodology**: 15 parallel sub-agent audits across all major subsystems

---

## Executive Summary

**D-CACHE NOW ENABLED** - System is ~50-100x faster. Remaining issues:

1. ✅ ~~DATA CACHE DISABLED~~ - **FIXED**
2. ✅ ~~Random hangs~~ - **FIXED** (timeouts added to mailbox/DMA waits)
3. ✅ ~~Flicker on fast redraws~~ - **FIXED** (vsync added)
4. ✅ ~~Desktop dirty tracking~~ - **FIXED**
5. **DMA not used** - Framebuffer and SD still use CPU loops
6. **FAT32 has O(n) algorithms** - File ops still slow
7. **VFS layer inefficiencies** - Partial reads load entire file
8. **Memory allocator is O(n)** - Every malloc/free scans the heap
9. **Scheduler is O(n)** - Scans all process slots in IRQ
10. **Sysmon kills performance** - 6+ heap scans per frame

---

## ✅ Critical Finding #0: DATA CACHE - FIXED

**Status**: COMPLETE (December 2024)

**Problem**: D-cache was disabled - every memory access hit RAM (~150 cycles vs ~2 cycles).

**Solution**: Enabled MMU with identity-mapped page tables. RAM regions marked cacheable, MMIO (0x3F000000+) marked as device/non-cacheable.

**Result**: ~50-100x speedup. System now draws faster than display can refresh (causes flicker - need vsync).

---

## Critical Finding #1: DMA Not Used

### Problem
The Pi has a DMA controller (BCM2837). We initialize it at boot. Then we never use it for any hot-path operations.

### Evidence

#### Framebuffer Clear (kernel/hal/pizero2w/fb.c:258)
```c
// CURRENT: Pixel-by-pixel loop - ~50ms for 800x600
for (uint32_t i = 0; i < width * virt_height; i++)
    fb_info.base[i] = 0x00000000;
```
- 480,000 individual 32-bit writes
- DMA could do this in ~2-3ms

#### Rectangle Fill (kernel/fb.c:61-70)
```c
// CURRENT: Per-row memset32
for (uint32_t row = 0; row < h; row++) {
    memset32(fb_base + (y + row) * fb_width + x, color, w);
}
```
- For 800x600 rect: 600 separate memset32 calls
- Should use 2D DMA (`hal_dma_copy_2d()`) for single atomic operation

#### Character Rendering (kernel/fb.c:81-101)
```c
// CURRENT: 128 individual pixel writes per character
row_ptr[0] = (row_data & 0x80) ? fg : bg;
row_ptr[1] = (row_data & 0x40) ? fg : bg;
// ... 8 writes per row, 16 rows = 128 writes
```
- Could batch to temp buffer, DMA to framebuffer

#### Console Scroll (kernel/console.c:80)
```c
// CURRENT: Full framebuffer memmove
memmove(fb_base, fb_base + line_pixels, ...);
```
- Copies 800 × 584 × 4 = 1.87MB per scroll
- Should use DMA for large memory moves

### Impact
- Boot: ~100ms wasted (double clear)
- Every UI draw: Hundreds of ms for complex screens
- Terminal: Visible lag on scroll

### Fix
1. ~~`hal_dma_fill()` - Add DMA fill operation for solid colors~~ **DONE** (partial - works in desktop, cache issues in term/console)
2. Use `hal_dma_copy_2d()` for rectangle fills
3. Use `hal_dma_copy()` for scroll operations
4. Batch character rendering to temp buffer, DMA to screen

---

## Critical Finding #2: SD Card Has No DMA

### Problem
The EMMC driver manually reads/writes the FIFO register 128 times per 512-byte block.

### Evidence

#### Single Block Read (kernel/hal/pizero2w/emmc.c:390-392)
```c
// CURRENT: 128 register reads per block
for (int i = 0; i < 128; i++) {
    buf32[i] = sdhci_read(REG_DATA);
}
```

#### Multi-Block Read (kernel/hal/pizero2w/emmc.c:484-486)
```c
// CURRENT: 128 reads per block, repeated for each block
for (int i = 0; i < 128; i++) {
    buf32[block * 128 + i] = sdhci_read(REG_DATA);
}
```

#### Clock Speed (kernel/hal/pizero2w/emmc.c:768-769)
```c
// CURRENT: Hard-coded 50 MHz
set_sd_clock(50000000);
```
- Pi Zero 2W can run SD at 100MHz safely
- 2x throughput available for free

#### Polling Delays (kernel/hal/pizero2w/emmc.c:264-266)
```c
// CURRENT: 1µs delays in tight loop
while (!(sdhci_read(REG_INTR) & INTR_CMD_DONE)) {
    if (--timeout == 0) return -1;
    delay_us(1);  // Up to 100,000 iterations = 100ms worst case
}
```

### Impact
- File reads: 128 register accesses per 512 bytes
- Large file loads: Seconds of delay
- FAT table traversal: Compounds with FAT32 issues

### Fix
1. Implement DMA mode using BCM2837 DMA controller
2. Use ADMA (Advanced DMA) if available in SDHCI
3. Increase clock to 100MHz after init
4. Use exponential backoff in polling loops

---

## Critical Finding #3: FAT32 Filesystem Disasters

### Problem
FAT32 implementation has multiple O(n) and O(n²) algorithms that compound with disk I/O.

### Evidence

#### Cluster Allocation Scans ALL Clusters (kernel/fat32.c:263-276)
```c
// CURRENT: Linear scan from cluster 2 to end
for (uint32_t cluster = 2; cluster < total_clusters; cluster++) {
    if (fat_next_cluster(cluster) == 0) {
        return cluster;  // Found free cluster
    }
}
```
- 64MB disk with 16KB clusters = ~4000 clusters
- Worst case: 4000 FAT reads to allocate one cluster
- Creating large files: O(n²) behavior

#### Free Space Check Scans ALL Clusters (kernel/fat32.c:1961-1974)
```c
// CURRENT: Full scan on every call
uint32_t fat32_get_free_kb(void) {
    uint32_t free_clusters = 0;
    for (uint32_t c = 2; c < total_clusters; c++) {
        if (fat_next_cluster(c) == 0) free_clusters++;
    }
    return free_clusters * cluster_size / 1024;
}
```
- Called by `mem` command, system monitor
- 4000+ FAT reads per call

#### Directory Scanned Multiple Times (multiple locations)
- `find_entry_in_dir()` - scans directory
- `find_free_dir_entries()` - scans same directory again
- `short_name_exists()` - scans again for LFN collision check
- Single file create can scan directory 3+ times

#### FAT Cache Too Small (kernel/fat32.c:60-67)
```c
#define FAT_CACHE_SIZE 8  // Only 8 sectors cached
```
- FAT table for 64MB disk = ~512KB (1024 sectors)
- 8-sector cache = 0.8% hit rate worst case

#### Write Amplification (kernel/fat32.c:244-254)
```c
// CURRENT: Two FAT writes per cluster allocation
fat_write_sector(fat_sector);           // FAT1
fat_write_sector(fat_sector + fat_size); // FAT2
fat_cache_invalidate();                  // Invalidate cache
```
- 1MB file with 64 clusters = 128 disk writes just for FAT
- Plus data writes, plus directory updates

### Impact
- File creation: O(n) to O(n²) depending on fragmentation
- Free space check: O(n) every time
- Large file writes: Massive write amplification
- Directory listings: Multiple redundant scans

### Fix (Priority Order)
1. **Cache free cluster count** - Increment/decrement on alloc/free
2. **Track last-allocated cluster** - Start search from there, not 0
3. **Increase FAT cache to 32-64 sectors**
4. **Batch FAT writes** - Update multiple entries before syncing
5. **Return entry offset from find_entry_in_dir()** - Reuse for updates
6. **Free cluster bitmap** - O(1) allocation with bitmap

---

## Critical Finding #4: VFS Layer Compounds Problems

### Problem
VFS layer adds additional inefficiencies on top of FAT32.

### Evidence

#### Partial Read = Full File Read (kernel/vfs.c:546-565)
```c
// CURRENT: Read entire file, copy portion
char *temp = malloc(file_size);
int read = fat32_read_file(filepath, temp, file_size);
memcpy(buf, temp + offset, to_copy);
free(temp);
```
- Reading 4 bytes from 1MB file = reads entire 1MB
- Catastrophic for large files

#### Append = Read + Copy + Write (kernel/vfs.c:622-642)
```c
// CURRENT: Full file round-trip
char *old_content = malloc(file_size);
fat32_read_file(path, old_content, file_size);
char *new_content = malloc(file_size + append_size);
memcpy(new_content, old_content, file_size);
memcpy(new_content + file_size, data, append_size);
fat32_write_file(path, new_content, new_size);
```
- Appending 1KB to 10MB file = 20MB I/O (read + write)

#### Double FAT32 Lookups (kernel/vfs.c:240, 261)
```c
// CURRENT: Two separate FAT32 calls
if (fat32_is_dir(normalized)) { ... }     // First lookup
size_t size = fat32_file_size(normalized); // Second lookup
```
- Same file traversed twice
- Should return both in single call

#### Path Normalization Duplicated (6+ places)
- `vfs_lookup()` - normalizes path
- `vfs_set_cwd()` - duplicates normalization
- `vfs_mkdir()` - duplicates again
- `vfs_create()` - duplicates again
- etc.

### Impact
- Small reads from large files: 100-1000x overhead
- Appends: O(file_size) instead of O(append_size)
- Every operation: Double disk traversals

### Fix
1. **Add offset parameter to fat32_read_file()** - Partial reads
2. **Implement true append mode** - Extend cluster chain
3. **Combine is_dir + file_size into single stat call**
4. **Extract path normalization to helper function**

---

## Critical Finding #5: Memory Allocator is O(n)

### Problem
The heap allocator uses a single linked list with linear search for all operations.

### Evidence

#### malloc Scans Entire Free List (kernel/memory.c:100-120)
```c
// CURRENT: Linear search
while (current != NULL) {
    if (current->is_free && current->size >= size) {
        // Found suitable block
    }
    current = current->next;
}
```
- Every allocation scans from beginning
- 1MB process stack allocation scans past all small blocks

#### free Scans for Coalescing (kernel/memory.c:134-144)
```c
// CURRENT: Another linear search
while (current != NULL) {
    if (current->is_free && adjacent_to(current, block)) {
        coalesce(current, block);
    }
    current = current->next;
}
```
- Every free triggers full list scan

#### Memory Stats are O(n) (kernel/memory.c:187-209)
```c
// CURRENT: Full scan for stats
uint32_t memory_used(void) {
    while (current != NULL) {
        if (!current->is_free) total += current->size;
        current = current->next;
    }
}
```
- Called by system monitor, `mem` command
- Hot path becomes O(n)

#### No Size Class Binning
- All free blocks in one list regardless of size
- 16-byte allocation searches past 1MB blocks

### Impact
- Frequent small allocations: O(n) per allocation
- System monitor refresh: O(n) per update
- Fragmented heap: Increasingly slow over time

### Fix
1. **Segregated free lists by size class**
   - Tiny: 16-64 bytes
   - Small: 64-256 bytes
   - Medium: 256-4KB
   - Large: 4KB-1MB
   - Huge: >1MB
2. **Boundary tags for O(1) coalescing**
3. **Running counters for memory stats**

---

## ✅ Critical Finding #6: Desktop Dirty Tracking - FIXED

**Status**: COMPLETE (December 2024)

Desktop now uses dirty rectangle tracking. Only changed regions are redrawn.

---

## Critical Finding #7: Scheduler is O(n) in IRQ Context

### Problem
The scheduler scans the entire process table on every scheduling decision, which happens in interrupt context.

### Evidence

#### Process Table Scan (kernel/process.c:464-490)
```c
// CURRENT: Linear search in IRQ handler
void process_schedule_from_irq(void) {
    // First: count ready processes (O(n))
    int ready = process_count_ready();

    // Then: find next ready process (O(n) again)
    for (int i = 0; i < MAX_PROCESSES; i++) {
        if (proc_table[i].state == READY) {
            // Found next process
        }
    }
}
```
- Called every 5 timer ticks (50ms)
- Two O(n) scans per call
- MAX_PROCESSES = 16, so 32 iterations minimum

#### Full FPU Save/Restore (kernel/context.S:82-124)
```c
// CURRENT: Always save all 32 FP registers
stp q0, q1, [x0, #0x90]
stp q2, q3, [x0, #0xB0]
// ... 16 more pairs
stp q30, q31, [x0, #0x190]
```
- 512 bytes saved/restored per context switch
- Most processes (shell, file manager) never use FPU
- Only calculator needs floating point

#### No Ready Queue
- No linked list of runnable processes
- Must scan all 16 slots to find any ready process
- O(1) would be possible with ready queue

### Impact
- Timer IRQ: 32+ memory accesses per tick
- Context switch: 1KB memory traffic (FPU regs)
- Scheduling latency: Adds microseconds per decision

### Fix
1. **Ready queue (linked list)**
   - O(1) to get next process
   - Update on spawn/exit/block/unblock
2. **Lazy FPU save**
   - Track if process used FPU
   - Only save/restore if dirty
3. **Cache current_process pointer**
   - Avoid repeated ADRP+ldr sequences

---

## Critical Finding #8: String Functions are Naive

### Problem
Core string functions don't use SIMD or word-width operations.

### Evidence

#### memcpy is 64-bit Only (kernel/string.c:8-31)
```c
// CURRENT: 64-bit copies
if (aligned && n >= 8) {
    while (n >= 8) {
        *d64++ = *s64++;
        n -= 8;
    }
}
```
- ARM NEON could do 128-bit (2x faster)
- No prefetching or cache hints

#### strlen is Byte-by-Byte (kernel/string.c:133-139)
```c
// CURRENT: Single byte per iteration
while (*s) {
    len++;
    s++;
}
```
- Could check 8 bytes at once using bit tricks
- "Find zero byte in word" is well-known optimization

#### strtok_r Delimiter Check is O(n×m) (kernel/string.c:236-242)
```c
// CURRENT: Linear search through delimiters
static int is_delim(char c, const char *delim) {
    while (*delim) {
        if (c == *delim) return 1;
        delim++;
    }
    return 0;
}
```
- Called for every character in string
- With 5 delimiters: 5 comparisons per character
- Could use 256-byte lookup table for O(1)

### Impact
- Large memcpy (framebuffer): 2x slower than possible
- Shell command parsing: Slower than needed
- String-heavy operations: Unnecessary overhead

### Fix
1. **NEON memcpy** - 128-bit loads/stores
2. **Word-width strlen** - Check 8 bytes per iteration
3. **Lookup table for delimiters** - O(1) instead of O(m)

---

## Critical Finding #9: USB Stack Over-Synchronizes

### Problem
USB driver uses excessive cache operations and memory barriers.

### Evidence

#### Multiple Cache Flushes per Transfer (kernel/hal/pizero2w/usb/usb_hid.c)
```c
// CURRENT: 3-4 cache ops per transfer
invalidate_data_cache_range(buffer, size);  // Before
// ... transfer ...
invalidate_data_cache_range(buffer, size);  // After
invalidate_data_cache_range(buffer, size);  // In ACK handler
```
- Cache operations are expensive on ARM
- One before + one after is sufficient

#### DSB After Every Register Write
```c
// CURRENT: Barrier after every write
HCCHAR(ch) = value;
dsb();  // Full system barrier
HCTSIZ(ch) = value;
dsb();  // Another barrier
```
- DSB SY is ~50-300 cycles
- Only needed at critical points, not every write

#### Split Transaction Waits 10ms (kernel/hal/pizero2w/usb/usb_hid.c:704-716)
```c
// CURRENT: Timer tick waits for microframe
if (frames_elapsed >= SPLIT_FRAME_WAIT) {
    // Complete split transaction
}
```
- Timer tick is 10ms
- USB microframe is 125µs
- Waiting 10ms for 1ms operation

#### Ring Buffer Uses Modulo (kernel/hal/pizero2w/usb/usb_hid.c:93-99)
```c
// CURRENT: Division in ISR
head = (head + 1) % KBD_RING_SIZE;
```
- Modulo requires division
- Should use bitmask with power-of-2 size

### Impact
- USB latency: Higher than necessary
- ISR time: Wasted on barriers
- Key repeat: Feels sluggish

### Fix
1. **Reduce cache ops to 2 per transfer** (before + after)
2. **Batch DSB barriers** - One per transfer, not per register
3. **Finer-grained split timing** - Use µs timer if available
4. **Bitmask for ring buffer** - `& (SIZE-1)` instead of `% SIZE`

---

## Critical Finding #10: Windowed Apps Redraw Everything

### Problem
GUI applications redraw their entire window on every event.

### Evidence

#### TextEdit: Full Redraw per Keystroke (user/bin/textedit.c:905)
```c
// CURRENT: Every key triggers full redraw
case WIN_EVENT_KEY:
    // Process key...
    draw_all();  // Redraws entire window
```
- Includes syntax highlighting parse of entire document
- 10KB file = 10,000 characters scanned per keystroke

#### Files: Full Redraw on Hover (user/bin/files.c:788-850)
```c
// CURRENT: Menu hover triggers full redraw
if (menu_hovered) {
    draw_all();
}
```
- File list, sidebar, scrollbar all redrawn
- Only menu highlight changed

#### Terminal: Full Clear on Cursor Blink (user/bin/term.c:263-321)
```c
// CURRENT: Clear entire window buffer
for (int i = 0; i < win_w * win_h; i++)
    win_buffer[i] = TERM_BG;
// Then redraw all 24x80 characters
```
- Called every 500ms for cursor blink
- Only cursor position needs update

#### Calculator: All Buttons Redrawn (user/bin/calc.c:315-328)
```c
// CURRENT: Button click redraws all buttons
case WIN_EVENT_MOUSE_UP:
    draw_all();  // Redraws all 16 buttons
```
- Only clicked button changed
- 15 buttons redrawn unnecessarily

### Impact
- TextEdit: Sluggish typing, especially on large files
- Files: Laggy menu navigation
- Terminal: Unnecessary work on blink
- Calculator: Slower than needed

### Fix
1. **Dirty line tracking in TextEdit** - Only redraw changed lines
2. **Per-region flags in Files** - Separate dirty for sidebar/list/menu
3. **Cursor-only update in Terminal** - XOR cursor, don't redraw all
4. **Single button redraw in Calculator** - Track which button changed

---

## Priority Matrix

| Priority | Category | Speedup | Effort | Dependencies |
|----------|----------|---------|--------|--------------|
| ~~P0~~ | ~~Desktop dirty rects~~ | ~~10x~~ | ~~Medium~~ | ✅ DONE |
| P1 | DMA fill (fix cache issues) | 10-50x | Medium | Works in desktop, broken in term/console |
| P1 | DMA for SD card | 4-8x | Medium | None |
| P1 | Sysmon O(1) counters | 10x | Low | None |
| P2 | FAT32 free cluster cache | 100x | Low | None |
| P2 | VFS partial read | 100x | Medium | FAT32 changes |
| P2 | Scheduler ready queue | 16x | Medium | None |
| P3 | Memory allocator bins | 5-10x | High | None |

---

## Recommended Attack Plan

### ✅ Phase 1: Critical Fixes - DONE
- D-cache enabled
- Infinite loop timeouts added
- Vsync implemented
- Desktop dirty tracking

### Phase 2: DMA Acceleration
**Goal**: Use the DMA hardware we already initialize

1. Use `hal_dma_fill()` for framebuffer clears
2. Update `fb_fill_rect()` to use 2D DMA
3. Update `console.c` scroll to use DMA copy
4. Add DMA to EMMC driver for block transfers

**Expected Result**: 10-50x faster screen and disk operations

### Phase 3: FAT32 Improvements
**Goal**: Eliminate O(n) scans

1. Cache free cluster count in memory
2. Track last-allocated cluster position
3. Increase FAT cache to 32 sectors
4. Add offset parameter to fat32_read_file() for partial reads

**Expected Result**: 10-100x faster file operations

### Phase 4: System Optimizations
**Goal**: Core infrastructure improvements

1. Add O(1) memory stat counters (fixes sysmon)
2. Scheduler ready queue
3. Lazy FPU save/restore

**Expected Result**: 2-10x faster system operations

---

## Appendix: Per-Component Details

### A. Pi Framebuffer Driver
- File: `kernel/hal/pizero2w/fb.c`
- Issues: Pixel loops, no DMA, double clear at boot
- Key lines: 258 (clear loop), mailbox overhead

### B. Pi EMMC Driver
- File: `kernel/hal/pizero2w/emmc.c`
- Issues: No DMA, 50MHz clock, polling delays
- Key lines: 390-392 (FIFO read), 768 (clock)

### C. FAT32 Filesystem
- File: `kernel/fat32.c`
- Issues: O(n) allocation, tiny cache, write amplification
- Key lines: 263-276 (alloc), 60 (cache size), 1961-1974 (free space)

### D. VFS Layer
- File: `kernel/vfs.c`
- Issues: Full file reads, append overhead, double lookups
- Key lines: 546-565 (read), 622-642 (append)

### E. Memory Allocator
- File: `kernel/memory.c`
- Issues: O(n) malloc/free, no bins, O(n) stats
- Key lines: 100-120 (malloc), 134-144 (free)

### F. Desktop - ✅ FIXED
- File: `user/bin/desktop.c`
- ~~Issues: No dirty tracking, per-pixel icons, full redraws~~
- **Status**: Dirty tracking implemented

### G. Scheduler
- File: `kernel/process.c`
- Issues: O(n) scanning, full FPU save, no ready queue
- Key lines: 464-490 (schedule), context.S FPU section

### H. String Functions
- File: `kernel/string.c`
- Issues: No SIMD, byte loops, O(n×m) delimiter check
- Key lines: 8-31 (memcpy), 133-139 (strlen)

### I. USB Stack
- Files: `kernel/hal/pizero2w/usb/*.c`
- Issues: Over-sync, excessive barriers, 10ms waits
- Key files: usb_hid.c, dwc2_core.c

### J. GUI Apps
- Files: `user/bin/{calc,files,textedit,term,sysmon,music,viewer}.c`
- Issues: Full redraws, no dirty tracking, syntax parse overhead
- Common pattern: `draw_all()` called too frequently

---

---

## Critical Finding #11: Sysmon Kills System Performance

### Problem
Sysmon calls expensive O(n) heap-scanning functions **6+ times per frame**, starving all other processes.

### Evidence

#### check_for_changes() (user/bin/sysmon.c:371-396)
```c
// Called EVERY frame before yield
api->get_mem_used();      // Scans entire heap
api->get_process_count(); // Scans proc_table
```

#### draw_all() (user/bin/sysmon.c:216-368)
```c
api->get_mem_used();      // Scans heap AGAIN
api->get_mem_free();      // Scans heap AGAIN
api->get_alloc_count();   // Scans heap A THIRD TIME
api->get_process_count(); // Scans proc_table AGAIN
// Then loops 16x calling get_process_info()
```

#### The O(n) Implementations (kernel/memory.c:187-235)
```c
uint32_t memory_used(void) {
    while (current != NULL) {  // Walks ENTIRE free list
        current = current->next;
    }
}
// memory_free() and memory_alloc_count() do the same
```

### Impact
- **6+ full heap scans per frame** when sysmon is running
- On fragmented heap with hundreds of blocks: catastrophic
- Other processes (term, desktop) get starved of CPU time
- Term can't even render its first frame

### Fix
1. Add running counters for memory stats (O(1) lookup)
2. Cache stats in sysmon, update once per second not per frame
3. Don't call expensive functions twice (check_for_changes AND draw_all)

---

## ✅ Critical Finding #12: Dock Hover - FIXED

**Status**: COMPLETE (December 2024)

Fixed as part of desktop dirty tracking implementation.

---

## ✅ Critical Finding #13: Infinite Loops - FIXED

**Status**: COMPLETE (December 2024)

All hardware waits now have timeouts. Mailbox read/write, DMA wait, and other loops will timeout and return error codes instead of hanging forever.

---

## Critical Finding #14: Term Blocks on spawn()

### Problem
When term launches vibesh, it blocks in `spawn()` while the ELF is loaded from SD card.

### Evidence

#### Term Startup (user/bin/term.c:500-504)
```c
redraw_screen();           // Draw initial frame
api->window_invalidate();
api->yield();
api->spawn("/bin/vibesh"); // BLOCKS HERE until ELF loaded
// Never gets back to event loop until spawn returns
```

#### spawn() is Synchronous (kernel/kapi.c:78-84)
```c
static int kapi_spawn(const char *path) {
    int pid = process_create(path, ...);  // Blocks on disk I/O
    process_start(pid);
    return pid;
}
```

#### process_create() Does Heavy Work (kernel/process.c:156-188)
```c
// All of this happens before spawn() returns:
vfs_read(file, data, size, 0);           // Read entire ELF from SD
elf_calc_size(data, size);               // Parse ELF
elf_load_at(data, size, load_addr, ...); // Load and relocate
malloc(PROCESS_STACK_SIZE);              // Allocate 1MB stack
```

### Impact
- Term can't render until spawn() returns
- On slow Pi SD card, ELF load takes significant time
- Timer preempts term before it can show anything
- User sees blank window

### Fix
1. Render after spawn returns (move redraw_screen after spawn)
2. Or make spawn truly async (queue load, return immediately)
3. Or preload common programs at boot

---

## Updated Priority Matrix

| Priority | Category | Speedup | Effort | Status |
|----------|----------|---------|--------|--------|
| ~~P0~~ | ~~D-CACHE + MMU~~ | ~~100x~~ | ~~Medium-High~~ | ✅ **DONE** |
| ~~P0~~ | ~~Infinite loop timeouts~~ | ~~∞~~ | ~~Low~~ | ✅ **DONE** |
| ~~P0~~ | ~~Vsync before buffer flip~~ | ~~Fixes flicker~~ | ~~Low~~ | ✅ **DONE** |
| ~~P1~~ | ~~Desktop dirty rectangles~~ | ~~5-10x~~ | ~~Medium~~ | ✅ **DONE** |
| **P1** | Sysmon memory stat caching | 10x for sysmon | Low | O(1) counters needed |
| **P1** | DMA fill (fix cache issues) | 10-50x | Medium | Works in desktop, cache issues in term/console |
| **P1** | DMA for SD card | 4-8x | Medium | FIFO reads are slow |
| **P2** | FAT32 free cluster cache | 10-100x | Low | Track count, don't rescan |
| **P2** | VFS partial read | 100x | Medium | Don't load entire file |
| **P2** | Scheduler ready queue | 16x | Medium | O(1) next process |
| **P3** | Memory allocator bins | 2-5x | High | Less critical now |

---

## Conclusion

### ✅ Major Issues Fixed - December 2024

1. **D-Cache enabled** - System 50-100x faster
2. **Infinite loop timeouts** - No more random hangs
3. **Vsync added** - No more flicker
4. **Desktop dirty tracking** - Efficient redraws

### Remaining Optimization Opportunities

**High Impact (P1)**:
- **Sysmon** - Add O(1) memory stat counters (Finding #11)
- **DMA fill** - Implemented, works in desktop, cache issues in term/console (Finding #1)
- **DMA for SD card** - Replace FIFO loops (Finding #2)

**Medium Impact (P2)**:
- **FAT32 caching** - Track free cluster count (Finding #3)
- **VFS partial reads** - Don't load entire file (Finding #4)
- **Scheduler ready queue** - O(1) process selection (Finding #7)

**Lower Priority (P3)**:
- **Memory allocator bins** - Segregated free lists (Finding #5)
