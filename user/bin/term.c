/*
 * term - VibeOS Terminal Emulator
 *
 * A windowed terminal that runs vibesh inside a desktop window.
 * Features:
 *   - Scrollback buffer (configurable lines)
 *   - Mouse wheel scrolling
 *   - Ctrl+C handling
 *   - Form feed (\f) for clear screen
 */

#include "../lib/vibe.h"

// Terminal dimensions (characters)
#define TERM_COLS 80
#define TERM_ROWS 24

// Scrollback buffer size (total lines including visible)
#define SCROLLBACK_LINES 500

// Character size
#define CHAR_WIDTH 8
#define CHAR_HEIGHT 16

// Window dimensions
#define WIN_WIDTH  (TERM_COLS * CHAR_WIDTH)
#define WIN_HEIGHT (TERM_ROWS * CHAR_HEIGHT)

// Colors (1-bit style)
#define TERM_BG 0x00FFFFFF
#define TERM_FG 0x00000000

// Global state
static kapi_t *api;
static int window_id = -1;
static uint32_t *win_buffer;
static int win_w, win_h;

// Scrollback buffer - ring buffer of lines
static char scrollback[SCROLLBACK_LINES][TERM_COLS];
static int scroll_head = 0;      // Next line to write to
static int scroll_count = 0;     // Total lines in buffer
static int scroll_offset = 0;    // How many lines scrolled back (0 = at bottom)

// Cursor position (relative to current write position, not view)
static int cursor_row = 0;       // Row within visible area
static int cursor_col = 0;

// Cursor blink state
static int cursor_visible = 1;
static unsigned long last_blink_tick = 0;

// Input buffer (ring buffer for keyboard input) - uses int for special keys
#define INPUT_BUF_SIZE 256
static int input_buffer[INPUT_BUF_SIZE];
static int input_head = 0;
static int input_tail = 0;

// Flag to track if shell is still running
static int shell_running = 1;

// Forward declarations
static void redraw_screen(void);

// ============ Scrollback Buffer Management ============

// Get the line index in scrollback buffer for a given display row
// row 0 is top of display, row TERM_ROWS-1 is bottom
static int get_line_index(int display_row) {
    // The visible area shows lines from (scroll_head - scroll_count + scroll_offset) to end
    // Actually, let's think about this differently:
    // - scroll_head points to where the NEXT line will be written
    // - The most recent line written is at scroll_head - 1
    // - The bottom of the display (when scroll_offset=0) shows the most recent lines

    // Bottom line of display = scroll_head - 1 - scroll_offset
    // Top line of display = bottom - TERM_ROWS + 1

    int bottom_line = scroll_head - 1 - scroll_offset;
    int target_line = bottom_line - (TERM_ROWS - 1 - display_row);

    // Wrap around
    while (target_line < 0) target_line += SCROLLBACK_LINES;
    target_line = target_line % SCROLLBACK_LINES;

    return target_line;
}

// Get pointer to a line in scrollback
static char *get_line(int display_row) {
    int idx = get_line_index(display_row);
    return scrollback[idx];
}

// Get the current write line (cursor row)
static char *get_write_line(void) {
    // Current write position is at scroll_head - (TERM_ROWS - cursor_row)
    int idx = scroll_head - (TERM_ROWS - cursor_row);
    while (idx < 0) idx += SCROLLBACK_LINES;
    idx = idx % SCROLLBACK_LINES;
    return scrollback[idx];
}

// Add a new line (scroll the terminal content up)
static void new_line(void) {
    // Clear the new line
    for (int i = 0; i < TERM_COLS; i++) {
        scrollback[scroll_head][i] = ' ';
    }

    scroll_head = (scroll_head + 1) % SCROLLBACK_LINES;
    if (scroll_count < SCROLLBACK_LINES) {
        scroll_count++;
    }

    // If we were scrolled back, stay at the same view position
    // (effectively scroll_offset increases by 1)
    // But if scroll_offset is 0, we stay at bottom
    if (scroll_offset > 0 && scroll_offset < scroll_count - TERM_ROWS) {
        scroll_offset++;
    }
}

// Clear the entire scrollback and screen
static void clear_all(void) {
    for (int i = 0; i < SCROLLBACK_LINES; i++) {
        for (int j = 0; j < TERM_COLS; j++) {
            scrollback[i][j] = ' ';
        }
    }
    scroll_head = TERM_ROWS;  // Leave room for visible area
    scroll_count = TERM_ROWS;
    scroll_offset = 0;
    cursor_row = 0;
    cursor_col = 0;
}

// ============ Drawing Functions ============

static void draw_char_at(int row, int col, char c) {
    if (row < 0 || row >= TERM_ROWS || col < 0 || col >= TERM_COLS) return;

    int px = col * CHAR_WIDTH;
    int py = row * CHAR_HEIGHT;

    const uint8_t *glyph = &api->font_data[(unsigned char)c * 16];

    for (int y = 0; y < CHAR_HEIGHT; y++) {
        for (int x = 0; x < CHAR_WIDTH; x++) {
            uint32_t color = (glyph[y] & (0x80 >> x)) ? TERM_FG : TERM_BG;
            int idx = (py + y) * win_w + (px + x);
            if (idx >= 0 && idx < win_w * win_h) {
                win_buffer[idx] = color;
            }
        }
    }
}

static void draw_cursor(void) {
    // Only draw cursor if we're at the bottom (not scrolled back) and visible
    if (scroll_offset != 0 || !cursor_visible) return;

    // Draw cursor as inverse block
    int px = cursor_col * CHAR_WIDTH;
    int py = cursor_row * CHAR_HEIGHT;

    for (int y = 0; y < CHAR_HEIGHT; y++) {
        for (int x = 0; x < CHAR_WIDTH; x++) {
            int idx = (py + y) * win_w + (px + x);
            if (idx >= 0 && idx < win_w * win_h) {
                // Invert the pixel
                win_buffer[idx] = win_buffer[idx] == TERM_BG ? TERM_FG : TERM_BG;
            }
        }
    }
}

// Update cursor blink state
static void update_cursor_blink(void) {
    unsigned long now = api->get_uptime_ticks();
    // Blink every 50 ticks (500ms at 100Hz)
    if (now - last_blink_tick >= 50) {
        cursor_visible = !cursor_visible;
        last_blink_tick = now;
        redraw_screen();
    }
}

static void redraw_screen(void) {
    // Clear buffer
    for (int i = 0; i < win_w * win_h; i++) {
        win_buffer[i] = TERM_BG;
    }

    // Draw all characters from scrollback
    for (int row = 0; row < TERM_ROWS; row++) {
        char *line = get_line(row);
        for (int col = 0; col < TERM_COLS; col++) {
            char c = line[col];
            if (c && c != ' ') {
                draw_char_at(row, col, c);
            }
        }
    }

    // Draw scrollback indicator if scrolled back
    if (scroll_offset > 0) {
        // Draw a small indicator in top-right
        char indicator[16];
        int lines_back = scroll_offset;
        // Simple integer to string
        int i = 0;
        indicator[i++] = '[';
        if (lines_back >= 100) indicator[i++] = '0' + (lines_back / 100) % 10;
        if (lines_back >= 10) indicator[i++] = '0' + (lines_back / 10) % 10;
        indicator[i++] = '0' + lines_back % 10;
        indicator[i++] = ']';
        indicator[i] = '\0';

        // Draw at top right, inverted
        int start_col = TERM_COLS - i;
        for (int j = 0; j < i && indicator[j]; j++) {
            // Draw inverted
            int px = (start_col + j) * CHAR_WIDTH;
            int py = 0;
            const uint8_t *glyph = &api->font_data[(unsigned char)indicator[j] * 16];
            for (int y = 0; y < CHAR_HEIGHT; y++) {
                for (int x = 0; x < CHAR_WIDTH; x++) {
                    uint32_t color = (glyph[y] & (0x80 >> x)) ? TERM_BG : TERM_FG;
                    int idx = (py + y) * win_w + (px + x);
                    if (idx >= 0 && idx < win_w * win_h) {
                        win_buffer[idx] = color;
                    }
                }
            }
        }
    }

    // Draw cursor
    draw_cursor();

    // Tell desktop to redraw
    api->window_invalidate(window_id);
}

// ============ Terminal Operations ============

static void term_putc(char c) {
    if (c == '\f') {
        // Form feed - clear screen
        clear_all();
        return;
    }

    if (c == '\n') {
        cursor_col = 0;
        cursor_row++;
        if (cursor_row >= TERM_ROWS) {
            cursor_row = TERM_ROWS - 1;
            new_line();
        }
        // Jump to bottom when outputting
        scroll_offset = 0;
        return;
    }

    if (c == '\r') {
        cursor_col = 0;
        return;
    }

    if (c == '\b') {
        if (cursor_col > 0) {
            cursor_col--;
        }
        return;
    }

    if (c == '\t') {
        cursor_col = (cursor_col + 8) & ~7;
        if (cursor_col >= TERM_COLS) {
            cursor_col = 0;
            cursor_row++;
            if (cursor_row >= TERM_ROWS) {
                cursor_row = TERM_ROWS - 1;
                new_line();
            }
        }
        return;
    }

    if (c >= 32 && c < 127) {
        // Printable character - write to current line
        char *line = get_write_line();
        line[cursor_col] = c;
        cursor_col++;
        if (cursor_col >= TERM_COLS) {
            cursor_col = 0;
            cursor_row++;
            if (cursor_row >= TERM_ROWS) {
                cursor_row = TERM_ROWS - 1;
                new_line();
            }
        }
        // Jump to bottom when outputting
        scroll_offset = 0;
    }
}

static void term_puts(const char *s) {
    while (*s) {
        term_putc(*s++);
    }
}

// ============ Stdio Hooks ============

static void stdio_hook_putc(char c) {
    term_putc(c);
    redraw_screen();
}

static void stdio_hook_puts(const char *s) {
    term_puts(s);
    redraw_screen();
}

static int stdio_hook_getc(void) {
    if (input_head == input_tail) {
        return -1;  // No input available
    }
    int c = input_buffer[input_head];
    input_head = (input_head + 1) % INPUT_BUF_SIZE;
    return c;
}

static int stdio_hook_has_key(void) {
    return input_head != input_tail;
}

// Add a key to input buffer
static void input_push(int c) {
    int next = (input_tail + 1) % INPUT_BUF_SIZE;
    if (next != input_head) {  // Not full
        input_buffer[input_tail] = c;
        input_tail = next;
    }
}

// ============ Scrolling ============

static void scroll_up(int lines) {
    // Scroll view back (show older content)
    int max_offset = scroll_count - TERM_ROWS;
    if (max_offset < 0) max_offset = 0;

    scroll_offset += lines;
    if (scroll_offset > max_offset) {
        scroll_offset = max_offset;
    }

    redraw_screen();
}

static void scroll_down(int lines) {
    // Scroll view forward (show newer content)
    scroll_offset -= lines;
    if (scroll_offset < 0) {
        scroll_offset = 0;
    }

    redraw_screen();
}

static void scroll_to_bottom(void) {
    scroll_offset = 0;
    redraw_screen();
}

// ============ Main ============

int main(kapi_t *kapi, int argc, char **argv) {
    (void)argc;
    (void)argv;

    api = kapi;

    // Check if window API is available
    if (!api->window_create) {
        api->puts("term: no window manager available\n");
        return 1;
    }

    // Create window
    window_id = api->window_create(50, 50, WIN_WIDTH, WIN_HEIGHT, "Terminal");
    if (window_id < 0) {
        api->puts("term: failed to create window\n");
        return 1;
    }

    // Get window buffer
    win_buffer = api->window_get_buffer(window_id, &win_w, &win_h);
    if (!win_buffer) {
        api->puts("term: failed to get window buffer\n");
        api->window_destroy(window_id);
        return 1;
    }

    // Initialize scrollback buffer
    clear_all();

    // Clear window to background color
    for (int i = 0; i < win_w * win_h; i++) {
        win_buffer[i] = TERM_BG;
    }

    // Register stdio hooks
    api->stdio_putc = stdio_hook_putc;
    api->stdio_puts = stdio_hook_puts;
    api->stdio_getc = stdio_hook_getc;
    api->stdio_has_key = stdio_hook_has_key;

    // Initial draw
    redraw_screen();

    // Spawn vibesh - it will use our stdio hooks
    int shell_pid = api->spawn("/bin/vibesh");
    if (shell_pid < 0) {
        term_puts("Failed to start shell!\n");
        redraw_screen();
    }

    // Track last mouse Y for scroll detection
    int last_mouse_y = -1;
    int mouse_scrolling = 0;

    // Main event loop
    while (shell_running) {
        // Poll window events
        int event_type, data1, data2, data3;
        while (api->window_poll_event(window_id, &event_type, &data1, &data2, &data3)) {
            if (event_type == WIN_EVENT_CLOSE) {
                shell_running = 0;
                break;
            }

            if (event_type == WIN_EVENT_KEY) {
                // Key pressed - add to input buffer (data1 is int, preserves special keys)
                input_push(data1);

                // Reset cursor blink to visible on keypress
                cursor_visible = 1;
                last_blink_tick = api->get_uptime_ticks();

                // If we're scrolled back and user types, jump to bottom
                if (scroll_offset > 0) {
                    scroll_to_bottom();
                }
            }

            if (event_type == WIN_EVENT_MOUSE_DOWN) {
                // Start tracking for scroll
                last_mouse_y = data2;  // data2 is y position
                mouse_scrolling = 1;
            }

            if (event_type == WIN_EVENT_MOUSE_UP) {
                mouse_scrolling = 0;
            }

            if (event_type == WIN_EVENT_MOUSE_MOVE) {
                // Check for scroll gesture (drag with button held)
                // data3 contains button state
                if (mouse_scrolling && (data3 & MOUSE_BTN_LEFT)) {
                    int dy = data2 - last_mouse_y;
                    if (dy < -CHAR_HEIGHT) {
                        // Dragged down = scroll up (show older)
                        scroll_up(1);
                        last_mouse_y = data2;
                    } else if (dy > CHAR_HEIGHT) {
                        // Dragged up = scroll down (show newer)
                        scroll_down(1);
                        last_mouse_y = data2;
                    }
                }
            }

            if (event_type == WIN_EVENT_RESIZE) {
                // Re-fetch buffer with new dimensions
                win_buffer = api->window_get_buffer(window_id, &win_w, &win_h);
                redraw_screen();
            }
        }

        // Update cursor blink
        update_cursor_blink();

        // Yield to other processes
        api->yield();
    }

    // Clean up stdio hooks
    api->stdio_putc = 0;
    api->stdio_puts = 0;
    api->stdio_getc = 0;
    api->stdio_has_key = 0;

    // Destroy window
    api->window_destroy(window_id);

    return 0;
}
