# Current Task: Fix Italic TTF Rendering

**Goal**: Fix the broken italic text rendering in the TTF system.

## Status: IN PROGRESS

### Background
Session 36 fixed a critical memory corruption bug in `apply_italic()` - the function was writing past the end of allocated buffers, corrupting the heap. The buffer overflow is now fixed, but italic text still renders as garbled garbage.

### What was fixed
- Buffer overflow in `apply_italic()` - was doubling the width expansion
- Cache eviction memory leak - now frees bitmap allocations
- Added memory debug functions to sysmon

### What's still broken
- Italic shear transform produces garbage output
- Bold rendering works fine
- Normal text works fine
- Only italic (and bold+italic) is broken

### Technical details
The `apply_italic()` function:
1. Takes a glyph bitmap with stride `alloc_w` and content width `content_w`
2. Should shear the bitmap ~12 degrees (0.2 factor)
3. Uses a temp buffer, applies shear, copies back
4. Currently the shear logic is not working correctly

### Files involved
- `kernel/ttf.c` - `apply_italic()` function (lines 174-205)
- The function signature: `apply_italic(bitmap, stride, content_w, h, &new_w)`

### Next steps
- Debug the shear transform logic
- Verify row/column indexing is correct
- Check temp_bitmap size is adequate
- Test with simple cases
