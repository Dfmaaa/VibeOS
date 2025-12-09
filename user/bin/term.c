/*
 * term - VibeOS Terminal Emulator
 *
 * A windowed terminal that runs vibesh inside a desktop window.
 * Provides character-based I/O through stdio hooks.
 */

#include "../lib/vibe.h"

// Terminal dimensions (characters)
#define TERM_COLS 80
#define TERM_ROWS 24

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

// Terminal state
static char screen[TERM_ROWS][TERM_COLS];  // Character buffer
static int cursor_row = 0;
static int cursor_col = 0;

// Input buffer (ring buffer for keyboard input)
#define INPUT_BUF_SIZE 256
static char input_buffer[INPUT_BUF_SIZE];
static int input_head = 0;
static int input_tail = 0;

// Flag to track if shell is still running
static int shell_running = 1;

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

static void redraw_screen(void) {
    // Clear buffer
    for (int i = 0; i < win_w * win_h; i++) {
        win_buffer[i] = TERM_BG;
    }

    // Draw all characters
    for (int row = 0; row < TERM_ROWS; row++) {
        for (int col = 0; col < TERM_COLS; col++) {
            char c = screen[row][col];
            if (c && c != ' ') {
                draw_char_at(row, col, c);
            }
        }
    }

    // Draw cursor
    draw_cursor();

    // Tell desktop to redraw
    api->window_invalidate(window_id);
}

// ============ Terminal Operations ============

static void scroll_up(void) {
    // Move all rows up by one
    for (int row = 0; row < TERM_ROWS - 1; row++) {
        for (int col = 0; col < TERM_COLS; col++) {
            screen[row][col] = screen[row + 1][col];
        }
    }
    // Clear bottom row
    for (int col = 0; col < TERM_COLS; col++) {
        screen[TERM_ROWS - 1][col] = ' ';
    }
}

static void newline(void) {
    cursor_col = 0;
    cursor_row++;
    if (cursor_row >= TERM_ROWS) {
        cursor_row = TERM_ROWS - 1;
        scroll_up();
    }
}

static void term_putc(char c) {
    if (c == '\n') {
        newline();
    } else if (c == '\r') {
        cursor_col = 0;
    } else if (c == '\b') {
        // Backspace
        if (cursor_col > 0) {
            cursor_col--;
        }
    } else if (c == '\t') {
        // Tab - move to next 8-char boundary
        cursor_col = (cursor_col + 8) & ~7;
        if (cursor_col >= TERM_COLS) {
            newline();
        }
    } else if (c >= 32 && c < 127) {
        // Printable character
        screen[cursor_row][cursor_col] = c;
        cursor_col++;
        if (cursor_col >= TERM_COLS) {
            newline();
        }
    }
}

static void term_puts(const char *s) {
    while (*s) {
        term_putc(*s++);
    }
}

// ============ Stdio Hooks ============

// These get registered in kapi so vibesh uses them

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
    char c = input_buffer[input_head];
    input_head = (input_head + 1) % INPUT_BUF_SIZE;
    return c;
}

static int stdio_hook_has_key(void) {
    return input_head != input_tail;
}

// Add a character to input buffer
static void input_push(char c) {
    int next = (input_tail + 1) % INPUT_BUF_SIZE;
    if (next != input_head) {  // Not full
        input_buffer[input_tail] = c;
        input_tail = next;
    }
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

    // Initialize screen buffer
    for (int row = 0; row < TERM_ROWS; row++) {
        for (int col = 0; col < TERM_COLS; col++) {
            screen[row][col] = ' ';
        }
    }

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
                // Key pressed - add to input buffer
                char c = (char)data1;
                input_push(c);
            }
            if (event_type == WIN_EVENT_RESIZE) {
                // Re-fetch buffer with new dimensions
                win_buffer = api->window_get_buffer(window_id, &win_w, &win_h);
                redraw_screen();
            }
        }

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
