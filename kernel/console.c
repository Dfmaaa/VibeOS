/*
 * VibeOS Text Console
 *
 * Provides terminal-like text output on the framebuffer.
 * Handles cursor positioning, scrolling, and basic escape sequences.
 */

#include "console.h"
#include "fb.h"
#include "font.h"
#include "string.h"
#include "printf.h"

// Console state
static int console_initialized = 0;
static int cursor_row = 0;
static int cursor_col = 0;
static int num_rows = 0;
static int num_cols = 0;
static uint32_t fg_color = COLOR_WHITE;
static uint32_t bg_color = COLOR_BLACK;

// Cursor blink state
static int cursor_visible = 0;
static int cursor_enabled = 1;

// Text buffer for scrolling
static char *text_buffer = NULL;
static uint32_t *fg_buffer = NULL;
static uint32_t *bg_buffer = NULL;

void console_init(void) {
    if (fb_base == NULL) return;

    // Calculate dimensions
    num_cols = fb_width / FONT_WIDTH;
    num_rows = fb_height / FONT_HEIGHT;

    // Allocate text buffer for scrollback
    // We'll just use static allocation for simplicity
    // In a real OS we'd use malloc

    cursor_row = 0;
    cursor_col = 0;

    // Don't clear screen - keep boot messages visible

    console_initialized = 1;
}

static void draw_char_at(int row, int col, char c) {
    uint32_t x = col * FONT_WIDTH;
    uint32_t y = row * FONT_HEIGHT;
    fb_draw_char(x, y, c, fg_color, bg_color);
}

static void scroll_up(void) {
    // Move all pixels up by one line
    uint32_t line_pixels = fb_width * FONT_HEIGHT;
    uint32_t total_pixels = fb_width * fb_height;

    // Copy pixels up using optimized memmove
    memmove(fb_base, fb_base + line_pixels, (total_pixels - line_pixels) * sizeof(uint32_t));

    // Clear the bottom line using optimized memset32
    memset32(fb_base + (total_pixels - line_pixels), bg_color, line_pixels);
}

static void newline(void) {
    cursor_col = 0;
    cursor_row++;

    if (cursor_row >= num_rows) {
        scroll_up();
        cursor_row = num_rows - 1;
    }
}

// Forward declaration
static void draw_cursor(int show);

void console_putc(char c) {
    // If console not initialized, fall back to UART
    if (!console_initialized) {
        extern void uart_putc(char c);
        if (c == '\n') uart_putc('\r');
        uart_putc(c);
        return;
    }

    // Hide cursor before any operation that might move it
    if (cursor_visible) {
        draw_cursor(0);
    }

    switch (c) {
        case '\n':
            newline();
            break;

        case '\r':
            cursor_col = 0;
            break;

        case '\t':
            // Tab to next 8-column boundary
            cursor_col = (cursor_col + 8) & ~7;
            if (cursor_col >= num_cols) {
                newline();
            }
            break;

        case '\b':
            // Backspace - only move cursor, don't erase
            if (cursor_col > 0) {
                cursor_col--;
            }
            break;

        default:
            if (c >= 32 && c < 127) {
                draw_char_at(cursor_row, cursor_col, c);
                cursor_col++;

                if (cursor_col >= num_cols) {
                    newline();
                }
            }
            break;
    }
}

void console_puts(const char *s) {
    // If no framebuffer, fall back to UART
    if (fb_base == NULL) {
        printf("%s", s);
        return;
    }
    while (*s) {
        console_putc(*s++);
    }
}

void console_clear(void) {
    fb_clear(bg_color);
    cursor_row = 0;
    cursor_col = 0;
}

void console_set_cursor(int row, int col) {
    // Hide cursor before moving
    if (cursor_visible) {
        draw_cursor(0);
    }
    if (row >= 0 && row < num_rows) cursor_row = row;
    if (col >= 0 && col < num_cols) cursor_col = col;
}

void console_get_cursor(int *row, int *col) {
    if (row) *row = cursor_row;
    if (col) *col = cursor_col;
}

void console_set_color(uint32_t fg, uint32_t bg) {
    fg_color = fg;
    bg_color = bg;
}

int console_rows(void) {
    return num_rows;
}

int console_cols(void) {
    return num_cols;
}

// Draw/undraw cursor at current position by inverting pixels
static void draw_cursor(int show) {
    if (!console_initialized || fb_base == NULL) return;
    if (show == cursor_visible) return;  // Already in desired state

    uint32_t x = cursor_col * FONT_WIDTH;
    uint32_t y = cursor_row * FONT_HEIGHT;

    // Toggle pixels (XOR-style invert)
    for (int dy = 0; dy < FONT_HEIGHT; dy++) {
        for (int dx = 0; dx < FONT_WIDTH; dx++) {
            uint32_t px = x + dx;
            uint32_t py = y + dy;
            if (px < fb_width && py < fb_height) {
                uint32_t *pixel = fb_base + py * fb_width + px;
                // Invert: swap fg and bg
                *pixel = (*pixel == bg_color) ? fg_color : bg_color;
            }
        }
    }
    cursor_visible = show;
}

// Toggle cursor visibility (called by timer)
void console_blink_cursor(void) {
    if (!cursor_enabled) return;
    draw_cursor(!cursor_visible);
}

// Enable/disable cursor
void console_set_cursor_enabled(int enabled) {
    if (!enabled && cursor_visible) {
        draw_cursor(0);  // Hide cursor
    }
    cursor_enabled = enabled;
}

// Force redraw cursor (call after moving cursor)
void console_show_cursor(void) {
    if (cursor_enabled && !cursor_visible) {
        draw_cursor(1);
    }
}
