/*
 * VibeOS Desktop - Window Manager
 *
 * Classic Mac System 7 aesthetic - TRUE 1-bit black & white.
 * Manages windows for GUI apps, dock, menu bar.
 *
 * Fullscreen apps (snake, tetris) are launched with exec() and take over.
 * Windowed apps use the window API registered in kapi.
 */

#include "vibe.h"

// Screen dimensions
#define SCREEN_WIDTH  800
#define SCREEN_HEIGHT 600

// UI dimensions
#define MENU_BAR_HEIGHT 20
#define DOCK_HEIGHT     52
#define TITLE_BAR_HEIGHT 20

// 1-bit Colors - TRUE Mac System 7 black & white
#define COLOR_BLACK      0x00000000
#define COLOR_WHITE      0x00FFFFFF

// Semantic color aliases (all B&W)
#define COLOR_DESKTOP    COLOR_BLACK   // We'll dither this
#define COLOR_MENU_BG    COLOR_WHITE
#define COLOR_MENU_TEXT  COLOR_BLACK
#define COLOR_TITLE_BG   COLOR_WHITE
#define COLOR_TITLE_TEXT COLOR_BLACK
#define COLOR_WIN_BG     COLOR_WHITE
#define COLOR_WIN_BORDER COLOR_BLACK
#define COLOR_DOCK_BG    COLOR_WHITE
#define COLOR_HIGHLIGHT  COLOR_BLACK

// Window limits
#define MAX_WINDOWS 16
#define MAX_TITLE_LEN 32

// Event structure
typedef struct {
    int type;
    int data1;
    int data2;
    int data3;
} win_event_t;

// Window structure
typedef struct {
    int active;           // Is this slot in use?
    int x, y, w, h;       // Position and size (including title bar)
    char title[MAX_TITLE_LEN];
    uint32_t *buffer;     // Content buffer (w * (h - TITLE_BAR_HEIGHT))
    int dirty;            // Needs redraw?
    int pid;              // Owner process ID (0 = desktop owns it)

    // Event queue (ring buffer)
    win_event_t events[32];
    int event_head;
    int event_tail;
} window_t;

// Dock icon
typedef struct {
    int x, y, w, h;
    const char *label;
    const char *exec_path;
    int is_fullscreen;    // If true, use exec() instead of spawn()
} dock_icon_t;

// Global state
static kapi_t *api;
static uint32_t *backbuffer;
static window_t windows[MAX_WINDOWS];
static int window_order[MAX_WINDOWS];  // Z-order: window_order[0] is topmost
static int window_count = 0;
static int focused_window = -1;

// Mouse state
static int mouse_x, mouse_y;
static int mouse_prev_x, mouse_prev_y;
static uint8_t mouse_buttons;
static uint8_t mouse_prev_buttons;

// Dragging state
static int dragging_window = -1;
static int drag_offset_x, drag_offset_y;

// Desktop running flag
static int running = 1;

// Forward declarations
static void draw_desktop(void);
static void draw_window(int wid);
static void draw_dock(void);
static void draw_menu_bar(void);
static void flip_buffer(void);

// ============ Backbuffer Drawing ============

static void bb_put_pixel(int x, int y, uint32_t color) {
    if (x >= 0 && x < SCREEN_WIDTH && y >= 0 && y < SCREEN_HEIGHT) {
        backbuffer[y * SCREEN_WIDTH + x] = color;
    }
}

static void bb_fill_rect(int x, int y, int w, int h, uint32_t color) {
    for (int py = y; py < y + h && py < SCREEN_HEIGHT; py++) {
        if (py < 0) continue;
        for (int px = x; px < x + w && px < SCREEN_WIDTH; px++) {
            if (px < 0) continue;
            backbuffer[py * SCREEN_WIDTH + px] = color;
        }
    }
}

static void bb_draw_char(int x, int y, char c, uint32_t fg, uint32_t bg) {
    const uint8_t *glyph = &api->font_data[(unsigned char)c * 16];
    for (int row = 0; row < 16; row++) {
        for (int col = 0; col < 8; col++) {
            uint32_t color = (glyph[row] & (0x80 >> col)) ? fg : bg;
            bb_put_pixel(x + col, y + row, color);
        }
    }
}

static void bb_draw_string(int x, int y, const char *s, uint32_t fg, uint32_t bg) {
    while (*s) {
        bb_draw_char(x, y, *s, fg, bg);
        x += 8;
        s++;
    }
}

static void bb_draw_hline(int x, int y, int w, uint32_t color) {
    for (int i = 0; i < w; i++) {
        bb_put_pixel(x + i, y, color);
    }
}

static void bb_draw_vline(int x, int y, int h, uint32_t color) {
    for (int i = 0; i < h; i++) {
        bb_put_pixel(x, y + i, color);
    }
}

static void bb_draw_rect(int x, int y, int w, int h, uint32_t color) {
    bb_draw_hline(x, y, w, color);
    bb_draw_hline(x, y + h - 1, w, color);
    bb_draw_vline(x, y, h, color);
    bb_draw_vline(x + w - 1, y, h, color);
}

// ============ Dither Patterns ============

// Classic Mac desktop pattern - diagonal checkerboard
static void bb_fill_pattern(int x, int y, int w, int h) {
    for (int py = y; py < y + h && py < SCREEN_HEIGHT; py++) {
        if (py < 0) continue;
        for (int px = x; px < x + w && px < SCREEN_WIDTH; px++) {
            if (px < 0) continue;
            // Classic Mac diagonal pattern
            int pattern = ((px + py) % 2 == 0) ? 1 : 0;
            backbuffer[py * SCREEN_WIDTH + px] = pattern ? COLOR_BLACK : COLOR_WHITE;
        }
    }
}

// 25% gray dither (sparser dots)
static void bb_fill_dither25(int x, int y, int w, int h) {
    for (int py = y; py < y + h && py < SCREEN_HEIGHT; py++) {
        if (py < 0) continue;
        for (int px = x; px < x + w && px < SCREEN_WIDTH; px++) {
            if (px < 0) continue;
            int pattern = ((px % 2 == 0) && (py % 2 == 0)) ? 1 : 0;
            backbuffer[py * SCREEN_WIDTH + px] = pattern ? COLOR_BLACK : COLOR_WHITE;
        }
    }
}

// ============ Apple Logo (16x16 bitmap) ============

// Classic rainbow Apple logo simplified to 1-bit
static const uint8_t apple_logo[16 * 16] = {
    0,0,0,0,0,0,0,1,1,0,0,0,0,0,0,0,
    0,0,0,0,0,0,1,1,1,0,0,0,0,0,0,0,
    0,0,0,0,0,0,1,1,0,0,0,0,0,0,0,0,
    0,0,0,1,1,1,1,1,1,1,1,1,0,0,0,0,
    0,0,1,1,1,1,1,1,1,1,1,1,1,0,0,0,
    0,1,1,1,1,1,1,1,1,1,1,1,1,1,0,0,
    0,1,1,1,1,1,1,1,1,1,1,1,1,1,0,0,
    0,1,1,1,1,1,1,1,1,1,1,1,1,1,0,0,
    0,1,1,1,1,1,1,1,1,1,1,1,1,1,0,0,
    0,1,1,1,1,1,1,1,1,1,1,1,1,1,0,0,
    0,1,1,1,1,1,1,1,1,1,1,1,1,1,0,0,
    0,0,1,1,1,1,1,1,1,1,1,1,1,0,0,0,
    0,0,1,1,1,1,1,1,1,1,1,1,1,0,0,0,
    0,0,0,1,1,1,1,1,1,1,1,1,0,0,0,0,
    0,0,0,0,1,1,1,0,1,1,1,0,0,0,0,0,
    0,0,0,0,0,1,0,0,0,1,0,0,0,0,0,0,
};

static void draw_apple_logo(int x, int y) {
    for (int py = 0; py < 16; py++) {
        for (int px = 0; px < 16; px++) {
            if (apple_logo[py * 16 + px]) {
                bb_put_pixel(x + px, y + py, COLOR_BLACK);
            }
        }
    }
}

// ============ Dock Icons (32x32 bitmaps) ============

// Snake icon - coiled snake
static const uint8_t icon_snake[32 * 32] = {
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,1,1,1,1,1,1,1,1,1,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,1,1,1,1,1,1,1,1,1,1,1,1,1,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,1,1,1,0,0,0,0,0,0,0,0,1,1,1,1,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,1,1,1,0,0,0,0,0,0,0,0,0,0,1,1,1,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,1,1,1,0,0,0,0,0,0,0,0,0,0,0,0,1,1,1,0,0,0,0,0,0,0,0,
    0,0,0,0,0,1,1,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,1,1,0,0,0,0,0,0,0,
    0,0,0,0,1,1,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,1,0,0,0,0,0,0,0,
    0,0,0,0,1,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,1,0,0,0,0,0,0,0,
    0,0,0,0,1,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,1,0,0,0,0,0,0,0,
    0,0,0,0,1,1,0,0,0,0,0,1,1,1,1,1,1,1,0,0,0,0,0,1,1,0,0,0,0,0,0,0,
    0,0,0,0,1,1,0,0,0,0,1,1,1,1,1,1,1,1,1,0,0,0,0,1,1,0,0,0,0,0,0,0,
    0,0,0,0,1,1,0,0,0,1,1,1,0,0,0,0,0,1,1,1,0,0,0,1,1,0,0,0,0,0,0,0,
    0,0,0,0,1,1,0,0,0,1,1,0,0,0,0,0,0,0,1,1,0,0,0,1,1,0,0,0,0,0,0,0,
    0,0,0,0,1,1,1,0,0,1,1,0,0,0,0,0,0,0,1,1,0,0,1,1,1,0,0,0,0,0,0,0,
    0,0,0,0,0,1,1,0,0,1,1,0,0,0,0,0,0,0,1,1,0,0,1,1,0,0,0,0,0,0,0,0,
    0,0,0,0,0,1,1,0,0,1,1,0,0,0,0,0,0,0,1,1,0,0,1,1,0,0,0,0,0,0,0,0,
    0,0,0,0,0,1,1,1,0,1,1,0,0,0,0,0,0,0,1,1,0,1,1,1,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,1,1,0,1,1,1,0,0,0,0,0,1,1,1,0,1,1,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,1,1,1,0,1,1,1,1,1,1,1,1,1,0,1,1,1,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,1,1,1,0,1,1,1,1,1,1,1,0,1,1,1,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,1,1,1,0,0,0,0,0,0,0,1,1,1,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,1,1,1,1,1,1,1,1,1,1,1,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,1,1,1,1,1,1,1,1,1,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,1,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,1,0,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,1,1,1,1,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,1,1,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
};

// Tetris icon - falling blocks
static const uint8_t icon_tetris[32 * 32] = {
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,1,1,1,1,1,1,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,1,1,1,1,1,1,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,1,1,0,0,0,1,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,1,1,0,0,0,1,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,1,1,1,1,1,1,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,1,1,1,1,1,1,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,1,1,1,1,1,1,1,1,1,1,1,1,1,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,1,1,1,1,1,1,1,1,1,1,1,1,1,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,1,1,0,0,0,1,1,0,0,0,1,1,1,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,1,1,0,0,0,1,1,0,0,0,1,1,1,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,1,1,1,1,1,1,1,1,1,1,1,1,1,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,1,1,1,1,1,1,1,1,1,1,1,1,1,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,1,1,1,1,1,1,1,1,1,1,1,1,1,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,1,1,1,1,1,1,1,1,1,1,1,1,1,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,1,0,0,0,1,1,0,0,0,1,1,1,1,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,1,0,0,0,1,1,0,0,0,1,1,1,1,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,1,0,0,0,1,1,0,0,0,1,1,1,1,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,1,0,0,0,1,1,0,0,0,1,1,1,1,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,1,1,1,1,1,1,1,1,1,1,1,1,1,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,1,1,1,1,1,1,1,1,1,1,1,1,1,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
};

// Calculator icon - cute retro calc
static const uint8_t icon_calc[32 * 32] = {
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,0,0,0,0,
    0,0,0,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,0,0,0,
    0,0,0,1,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,1,0,0,0,
    0,0,0,1,1,0,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,0,1,1,0,0,0,
    0,0,0,1,1,0,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,0,1,1,0,0,0,
    0,0,0,1,1,0,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,0,1,1,0,0,0,
    0,0,0,1,1,0,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,0,1,1,0,0,0,
    0,0,0,1,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,1,0,0,0,
    0,0,0,1,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,1,0,0,0,
    0,0,0,1,1,0,1,1,1,1,0,0,1,1,1,1,0,0,1,1,1,1,0,0,1,1,0,1,1,0,0,0,
    0,0,0,1,1,0,1,1,1,1,0,0,1,1,1,1,0,0,1,1,1,1,0,0,1,1,0,1,1,0,0,0,
    0,0,0,1,1,0,1,1,1,1,0,0,1,1,1,1,0,0,1,1,1,1,0,0,1,1,0,1,1,0,0,0,
    0,0,0,1,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,1,0,1,1,0,0,0,
    0,0,0,1,1,0,1,1,1,1,0,0,1,1,1,1,0,0,1,1,1,1,0,0,1,1,0,1,1,0,0,0,
    0,0,0,1,1,0,1,1,1,1,0,0,1,1,1,1,0,0,1,1,1,1,0,0,1,1,0,1,1,0,0,0,
    0,0,0,1,1,0,1,1,1,1,0,0,1,1,1,1,0,0,1,1,1,1,0,0,1,1,0,1,1,0,0,0,
    0,0,0,1,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,1,0,1,1,0,0,0,
    0,0,0,1,1,0,1,1,1,1,0,0,1,1,1,1,0,0,1,1,1,1,0,0,1,1,0,1,1,0,0,0,
    0,0,0,1,1,0,1,1,1,1,0,0,1,1,1,1,0,0,1,1,1,1,0,0,1,1,0,1,1,0,0,0,
    0,0,0,1,1,0,1,1,1,1,0,0,1,1,1,1,0,0,1,1,1,1,0,0,1,1,0,1,1,0,0,0,
    0,0,0,1,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,1,0,1,1,0,0,0,
    0,0,0,1,1,0,1,1,1,1,0,0,1,1,1,1,0,0,1,1,1,1,0,0,1,1,0,1,1,0,0,0,
    0,0,0,1,1,0,1,1,1,1,0,0,1,1,1,1,0,0,1,1,1,1,0,0,1,1,0,1,1,0,0,0,
    0,0,0,1,1,0,1,1,1,1,0,0,1,1,1,1,0,0,1,1,1,1,0,0,1,1,0,1,1,0,0,0,
    0,0,0,1,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,1,0,0,0,
    0,0,0,1,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,1,0,0,0,
    0,0,0,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,0,0,0,
    0,0,0,0,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
};

// Files/Folder icon - classic Mac folder
static const uint8_t icon_files[32 * 32] = {
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,1,1,1,1,1,1,1,1,1,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,1,1,0,0,0,0,0,0,0,0,1,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,1,1,0,0,0,0,0,0,0,0,0,0,1,1,1,1,1,1,1,1,1,1,1,1,1,1,0,0,0,0,
    0,0,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,1,0,0,0,
    0,0,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,0,0,0,
    0,0,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,0,0,0,
    0,0,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,0,0,0,
    0,0,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,0,0,0,
    0,0,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,0,0,0,
    0,0,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,0,0,0,
    0,0,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,0,0,0,
    0,0,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,0,0,0,
    0,0,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,0,0,0,
    0,0,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,0,0,0,
    0,0,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,0,0,0,
    0,0,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,0,0,0,
    0,0,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,0,0,0,
    0,0,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,0,0,0,
    0,0,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,0,0,0,
    0,0,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,0,0,0,
    0,0,1,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,1,0,0,0,
    0,0,0,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
};

// Terminal icon - classic CRT monitor with >_ prompt
static const uint8_t icon_term[32 * 32] = {
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,0,0,
    0,0,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,0,0,
    0,0,1,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,1,0,0,
    0,0,1,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,1,0,0,
    0,0,1,1,0,0,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,1,0,0,
    0,0,1,1,0,0,0,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,1,0,0,
    0,0,1,1,0,0,0,0,1,0,1,1,1,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,1,0,0,
    0,0,1,1,0,0,0,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,1,0,0,
    0,0,1,1,0,0,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,1,0,0,
    0,0,1,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,1,0,0,
    0,0,1,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,1,0,0,
    0,0,1,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,1,0,0,
    0,0,1,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,1,0,0,
    0,0,1,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,1,0,0,
    0,0,1,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,1,0,0,
    0,0,1,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,1,0,0,
    0,0,1,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,1,0,0,
    0,0,1,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,1,0,0,
    0,0,1,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,1,0,0,
    0,0,1,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,1,0,0,
    0,0,1,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,1,0,0,
    0,0,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,0,0,
    0,0,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,1,1,1,1,1,1,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,1,1,1,1,1,1,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,1,1,1,1,1,1,1,1,1,1,1,1,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,1,1,1,1,1,1,1,1,1,1,1,1,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
};

// Icon data array for dock
static const uint8_t *icon_bitmaps[] = {
    icon_snake,
    icon_tetris,
    icon_calc,
    icon_files,
    icon_term,
};

static void draw_icon_bitmap(int x, int y, const uint8_t *bitmap, int inverted) {
    uint32_t fg = inverted ? COLOR_WHITE : COLOR_BLACK;
    uint32_t bg = inverted ? COLOR_BLACK : COLOR_WHITE;

    for (int py = 0; py < 32; py++) {
        for (int px = 0; px < 32; px++) {
            uint32_t color = bitmap[py * 32 + px] ? fg : bg;
            bb_put_pixel(x + px, y + py, color);
        }
    }
}

// ============ Window Management ============

static int find_free_window(void) {
    for (int i = 0; i < MAX_WINDOWS; i++) {
        if (!windows[i].active) return i;
    }
    return -1;
}

static void bring_to_front(int wid) {
    if (wid < 0 || !windows[wid].active) return;

    // Find current position in z-order
    int pos = -1;
    for (int i = 0; i < window_count; i++) {
        if (window_order[i] == wid) {
            pos = i;
            break;
        }
    }

    if (pos < 0) return;

    // Shift everything down and put this at front
    for (int i = pos; i > 0; i--) {
        window_order[i] = window_order[i - 1];
    }
    window_order[0] = wid;
    focused_window = wid;
}

static int window_at_point(int x, int y) {
    // Check in z-order (front to back)
    for (int i = 0; i < window_count; i++) {
        int wid = window_order[i];
        window_t *w = &windows[wid];
        if (w->active) {
            if (x >= w->x && x < w->x + w->w &&
                y >= w->y && y < w->y + w->h) {
                return wid;
            }
        }
    }
    return -1;
}

static void push_event(int wid, int event_type, int data1, int data2, int data3) {
    if (wid < 0 || !windows[wid].active) return;
    window_t *w = &windows[wid];

    int next = (w->event_tail + 1) % 32;
    if (next == w->event_head) return;  // Queue full

    w->events[w->event_tail].type = event_type;
    w->events[w->event_tail].data1 = data1;
    w->events[w->event_tail].data2 = data2;
    w->events[w->event_tail].data3 = data3;
    w->event_tail = next;
}

// ============ Window API (registered in kapi) ============

static int wm_window_create(int x, int y, int w, int h, const char *title) {
    int wid = find_free_window();
    if (wid < 0) return -1;

    window_t *win = &windows[wid];
    win->active = 1;
    win->x = x;
    win->y = y;
    win->w = w;
    win->h = h;
    win->dirty = 1;
    win->pid = 0;  // TODO: get current process
    win->event_head = 0;
    win->event_tail = 0;

    // Copy title
    int i;
    for (i = 0; i < MAX_TITLE_LEN - 1 && title[i]; i++) {
        win->title[i] = title[i];
    }
    win->title[i] = '\0';

    // Allocate content buffer (excluding title bar)
    int content_h = h - TITLE_BAR_HEIGHT;
    if (content_h < 1) content_h = 1;
    win->buffer = api->malloc(w * content_h * sizeof(uint32_t));
    if (!win->buffer) {
        win->active = 0;
        return -1;
    }

    // Clear to white
    for (int j = 0; j < w * content_h; j++) {
        win->buffer[j] = COLOR_WIN_BG;
    }

    // Add to z-order (at front)
    for (int j = window_count; j > 0; j--) {
        window_order[j] = window_order[j - 1];
    }
    window_order[0] = wid;
    window_count++;
    focused_window = wid;

    return wid;
}

static void wm_window_destroy(int wid) {
    if (wid < 0 || wid >= MAX_WINDOWS || !windows[wid].active) return;

    window_t *win = &windows[wid];
    if (win->buffer) {
        api->free(win->buffer);
        win->buffer = 0;
    }
    win->active = 0;

    // Remove from z-order
    int pos = -1;
    for (int i = 0; i < window_count; i++) {
        if (window_order[i] == wid) {
            pos = i;
            break;
        }
    }
    if (pos >= 0) {
        for (int i = pos; i < window_count - 1; i++) {
            window_order[i] = window_order[i + 1];
        }
        window_count--;
    }

    // Update focus
    if (focused_window == wid) {
        focused_window = (window_count > 0) ? window_order[0] : -1;
    }
}

static uint32_t *wm_window_get_buffer(int wid, int *w, int *h) {
    if (wid < 0 || wid >= MAX_WINDOWS || !windows[wid].active) return 0;
    window_t *win = &windows[wid];
    if (w) *w = win->w;
    if (h) *h = win->h - TITLE_BAR_HEIGHT;
    return win->buffer;
}

static int wm_window_poll_event(int wid, int *event_type, int *data1, int *data2, int *data3) {
    if (wid < 0 || wid >= MAX_WINDOWS || !windows[wid].active) return 0;
    window_t *win = &windows[wid];

    if (win->event_head == win->event_tail) return 0;  // No events

    win_event_t *ev = &win->events[win->event_head];
    *event_type = ev->type;
    *data1 = ev->data1;
    *data2 = ev->data2;
    *data3 = ev->data3;
    win->event_head = (win->event_head + 1) % 32;
    return 1;
}

static void wm_window_invalidate(int wid) {
    if (wid < 0 || wid >= MAX_WINDOWS || !windows[wid].active) return;
    windows[wid].dirty = 1;
}

static void wm_window_set_title(int wid, const char *title) {
    if (wid < 0 || wid >= MAX_WINDOWS || !windows[wid].active) return;
    window_t *win = &windows[wid];
    int i;
    for (i = 0; i < MAX_TITLE_LEN - 1 && title[i]; i++) {
        win->title[i] = title[i];
    }
    win->title[i] = '\0';
    win->dirty = 1;
}

// ============ Dock ============

#define DOCK_ICON_SIZE 32
#define DOCK_PADDING 12
#define DOCK_LABEL_HEIGHT 12

// Dock icons with bitmap indices
static dock_icon_t dock_icons[] = {
    { 0, 0, DOCK_ICON_SIZE, DOCK_ICON_SIZE, "Snake",  "/bin/snake",  1 },
    { 0, 0, DOCK_ICON_SIZE, DOCK_ICON_SIZE, "Tetris", "/bin/tetris", 1 },
    { 0, 0, DOCK_ICON_SIZE, DOCK_ICON_SIZE, "Calc",   "/bin/calc",   0 },
    { 0, 0, DOCK_ICON_SIZE, DOCK_ICON_SIZE, "Files",  "/bin/files",  0 },
    { 0, 0, DOCK_ICON_SIZE, DOCK_ICON_SIZE, "Term",   "/bin/term",   0 },
};
#define NUM_DOCK_ICONS (sizeof(dock_icons) / sizeof(dock_icons[0]))

static void init_dock_positions(void) {
    int total_width = NUM_DOCK_ICONS * (DOCK_ICON_SIZE + DOCK_PADDING) - DOCK_PADDING;
    int start_x = (SCREEN_WIDTH - total_width) / 2;
    int y = SCREEN_HEIGHT - DOCK_HEIGHT + 6;  // Top padding

    for (int i = 0; i < (int)NUM_DOCK_ICONS; i++) {
        dock_icons[i].x = start_x + i * (DOCK_ICON_SIZE + DOCK_PADDING);
        dock_icons[i].y = y;
    }
}

static void draw_dock_icon(dock_icon_t *icon, int icon_idx, int highlight) {
    // Draw the bitmap icon
    draw_icon_bitmap(icon->x, icon->y, icon_bitmaps[icon_idx], highlight);

    // Draw label below icon
    int label_len = strlen(icon->label);
    int label_x = icon->x + (DOCK_ICON_SIZE - label_len * 8) / 2;
    int label_y = icon->y + DOCK_ICON_SIZE + 2;

    // Label background (inverted if highlighted)
    if (highlight) {
        bb_fill_rect(label_x - 2, label_y - 1, label_len * 8 + 4, 10, COLOR_BLACK);
        bb_draw_string(label_x, label_y, icon->label, COLOR_WHITE, COLOR_BLACK);
    } else {
        bb_draw_string(label_x, label_y, icon->label, COLOR_BLACK, COLOR_WHITE);
    }
}

static void draw_dock(void) {
    // Dock background - white with top border
    bb_fill_rect(0, SCREEN_HEIGHT - DOCK_HEIGHT, SCREEN_WIDTH, DOCK_HEIGHT, COLOR_WHITE);
    // Double line at top for 3D effect
    bb_draw_hline(0, SCREEN_HEIGHT - DOCK_HEIGHT, SCREEN_WIDTH, COLOR_BLACK);
    bb_draw_hline(0, SCREEN_HEIGHT - DOCK_HEIGHT + 2, SCREEN_WIDTH, COLOR_BLACK);

    // Icons
    for (int i = 0; i < (int)NUM_DOCK_ICONS; i++) {
        int highlight = (mouse_y >= dock_icons[i].y &&
                        mouse_y < dock_icons[i].y + DOCK_ICON_SIZE + DOCK_LABEL_HEIGHT &&
                        mouse_x >= dock_icons[i].x &&
                        mouse_x < dock_icons[i].x + DOCK_ICON_SIZE);
        draw_dock_icon(&dock_icons[i], i, highlight);
    }
}

static int dock_icon_at_point(int x, int y) {
    for (int i = 0; i < (int)NUM_DOCK_ICONS; i++) {
        if (x >= dock_icons[i].x && x < dock_icons[i].x + DOCK_ICON_SIZE &&
            y >= dock_icons[i].y && y < dock_icons[i].y + DOCK_ICON_SIZE) {
            return i;
        }
    }
    return -1;
}

// ============ Menu Bar ============

static void draw_menu_bar(void) {
    // Background
    bb_fill_rect(0, 0, SCREEN_WIDTH, MENU_BAR_HEIGHT, COLOR_MENU_BG);
    // Bottom border - double line for 3D effect
    bb_draw_hline(0, MENU_BAR_HEIGHT - 2, SCREEN_WIDTH, COLOR_BLACK);
    bb_draw_hline(0, MENU_BAR_HEIGHT - 1, SCREEN_WIDTH, COLOR_BLACK);

    // Apple logo in menu bar
    draw_apple_logo(4, 2);

    // Menu items with proper spacing
    bb_draw_string(26, 2, "File", COLOR_MENU_TEXT, COLOR_MENU_BG);
    bb_draw_string(66, 2, "Edit", COLOR_MENU_TEXT, COLOR_MENU_BG);
    bb_draw_string(106, 2, "View", COLOR_MENU_TEXT, COLOR_MENU_BG);
    bb_draw_string(154, 2, "Special", COLOR_MENU_TEXT, COLOR_MENU_BG);

    // Clock on right side (just for aesthetics)
    bb_draw_string(SCREEN_WIDTH - 52, 2, "12:00", COLOR_MENU_TEXT, COLOR_MENU_BG);
}

// ============ Window Drawing ============

// Draw System 7 style horizontal stripes for title bar
static void draw_title_stripes(int x, int y, int w, int h) {
    for (int row = 0; row < h; row++) {
        // Every other row is black (creates stripe effect)
        if (row % 2 == 1) {
            for (int col = 0; col < w; col++) {
                bb_put_pixel(x + col, y + row, COLOR_BLACK);
            }
        } else {
            for (int col = 0; col < w; col++) {
                bb_put_pixel(x + col, y + row, COLOR_WHITE);
            }
        }
    }
}

static void draw_window(int wid) {
    if (wid < 0 || !windows[wid].active) return;
    window_t *w = &windows[wid];

    int is_focused = (wid == focused_window);

    // Outer shadow (drop shadow effect)
    bb_fill_rect(w->x + 2, w->y + w->h, w->w, 2, COLOR_BLACK);
    bb_fill_rect(w->x + w->w, w->y + 2, 2, w->h, COLOR_BLACK);

    // Window background
    bb_fill_rect(w->x, w->y, w->w, w->h, COLOR_WHITE);

    // Window border (double line)
    bb_draw_rect(w->x, w->y, w->w, w->h, COLOR_BLACK);
    bb_draw_rect(w->x + 1, w->y + 1, w->w - 2, w->h - 2, COLOR_BLACK);

    // Title bar area
    if (is_focused) {
        // Striped title bar (System 7 signature look)
        // Leave space for close box and title
        int stripe_start = w->x + 20;  // After close box
        int stripe_end = w->x + w->w - 20;  // Before right edge
        int title_len = strlen(w->title);
        int title_width = title_len * 8 + 8;  // Title + padding
        int title_start = w->x + (w->w - title_width) / 2;

        // Left stripes
        draw_title_stripes(stripe_start, w->y + 4, title_start - stripe_start - 4, TITLE_BAR_HEIGHT - 8);

        // Right stripes
        draw_title_stripes(title_start + title_width + 4, w->y + 4,
                          stripe_end - (title_start + title_width + 4), TITLE_BAR_HEIGHT - 8);
    }

    // Title bar bottom line
    bb_draw_hline(w->x + 1, w->y + TITLE_BAR_HEIGHT, w->w - 2, COLOR_BLACK);

    // Close box (left side) - System 7 style with inner box
    int close_x = w->x + 6;
    int close_y = w->y + 4;
    bb_fill_rect(close_x, close_y, 13, 13, COLOR_WHITE);
    bb_draw_rect(close_x, close_y, 13, 13, COLOR_BLACK);
    if (is_focused) {
        // Inner box when focused
        bb_draw_rect(close_x + 3, close_y + 3, 7, 7, COLOR_BLACK);
    }

    // Title text (centered, bold effect with double-draw)
    int title_len = strlen(w->title);
    int title_x = w->x + (w->w - title_len * 8) / 2;
    int title_y = w->y + 3;
    bb_draw_string(title_x, title_y, w->title, COLOR_BLACK, COLOR_WHITE);

    // Content area - copy from window buffer
    int content_y = w->y + TITLE_BAR_HEIGHT + 2;
    int content_h = w->h - TITLE_BAR_HEIGHT - 4;
    int content_w = w->w - 4;

    for (int py = 0; py < content_h; py++) {
        for (int px = 0; px < content_w; px++) {
            int screen_x = w->x + 2 + px;
            int screen_y = content_y + py;
            if (screen_x < SCREEN_WIDTH && screen_y < SCREEN_HEIGHT) {
                backbuffer[screen_y * SCREEN_WIDTH + screen_x] =
                    w->buffer[py * (w->w) + px];
            }
        }
    }
}

// ============ Cursor ============

static void draw_cursor(int x, int y) {
    // Classic Mac-style arrow cursor as flat array (PIE-safe)
    // 1 = black, 2 = white, 0 = transparent
    static const uint8_t cursor_bits[16 * 16] = {
        1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
        1,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
        1,2,1,0,0,0,0,0,0,0,0,0,0,0,0,0,
        1,2,2,1,0,0,0,0,0,0,0,0,0,0,0,0,
        1,2,2,2,1,0,0,0,0,0,0,0,0,0,0,0,
        1,2,2,2,2,1,0,0,0,0,0,0,0,0,0,0,
        1,2,2,2,2,2,1,0,0,0,0,0,0,0,0,0,
        1,2,2,2,2,2,2,1,0,0,0,0,0,0,0,0,
        1,2,2,2,2,2,2,2,1,0,0,0,0,0,0,0,
        1,2,2,2,2,2,2,2,2,1,0,0,0,0,0,0,
        1,2,2,2,2,2,1,1,1,1,1,0,0,0,0,0,
        1,2,2,1,2,2,1,0,0,0,0,0,0,0,0,0,
        1,2,1,1,2,2,1,0,0,0,0,0,0,0,0,0,
        1,1,0,0,1,2,2,1,0,0,0,0,0,0,0,0,
        1,0,0,0,0,1,2,2,1,0,0,0,0,0,0,0,
        0,0,0,0,0,0,1,1,0,0,0,0,0,0,0,0,
    };

    for (int py = 0; py < 16; py++) {
        for (int px = 0; px < 16; px++) {
            uint8_t c = cursor_bits[py * 16 + px];
            if (c != 0) {
                int sx = x + px;
                int sy = y + py;
                if (sx >= 0 && sx < SCREEN_WIDTH && sy >= 0 && sy < SCREEN_HEIGHT) {
                    uint32_t color = (c == 1) ? COLOR_BLACK : COLOR_WHITE;
                    backbuffer[sy * SCREEN_WIDTH + sx] = color;
                }
            }
        }
    }
}

// ============ Main Drawing ============

static void draw_desktop(void) {
    // Desktop background - classic Mac diagonal checkerboard pattern
    bb_fill_pattern(0, MENU_BAR_HEIGHT, SCREEN_WIDTH,
                    SCREEN_HEIGHT - MENU_BAR_HEIGHT - DOCK_HEIGHT);

    // Menu bar
    draw_menu_bar();

    // Windows (back to front)
    for (int i = window_count - 1; i >= 0; i--) {
        draw_window(window_order[i]);
    }

    // Dock
    draw_dock();
}

static void flip_buffer(void) {
    memcpy(api->fb_base, backbuffer, SCREEN_WIDTH * SCREEN_HEIGHT * sizeof(uint32_t));
}

// ============ Input Handling ============

static void handle_mouse_click(int x, int y, uint8_t buttons) {
    // Check dock first (left click only)
    if (buttons & MOUSE_BTN_LEFT) {
        int dock_idx = dock_icon_at_point(x, y);
        if (dock_idx >= 0) {
            dock_icon_t *icon = &dock_icons[dock_idx];
            if (icon->is_fullscreen) {
                // Fullscreen app - exec and wait
                api->exec(icon->exec_path);
                // When we return, redraw everything
            } else {
                // Windowed app - spawn
                api->spawn(icon->exec_path);
            }
            return;
        }
    }

    // Check windows
    int wid = window_at_point(x, y);
    if (wid >= 0) {
        window_t *w = &windows[wid];
        bring_to_front(wid);

        // Check if click is on title bar (left click only for dragging/close)
        if ((buttons & MOUSE_BTN_LEFT) && y >= w->y && y < w->y + TITLE_BAR_HEIGHT) {
            // Check close box (updated position)
            int close_x = w->x + 6;
            int close_y = w->y + 4;
            if (x >= close_x && x < close_x + 13 &&
                y >= close_y && y < close_y + 13) {
                // Close window
                push_event(wid, WIN_EVENT_CLOSE, 0, 0, 0);
                return;
            }

            // Start dragging
            dragging_window = wid;
            drag_offset_x = x - w->x;
            drag_offset_y = y - w->y;
        } else if (y >= w->y + TITLE_BAR_HEIGHT) {
            // Click in content area - send event to app with button info
            int local_x = x - w->x - 1;
            int local_y = y - w->y - TITLE_BAR_HEIGHT - 1;
            push_event(wid, WIN_EVENT_MOUSE_DOWN, local_x, local_y, buttons);
        }
    }
}

static void handle_mouse_release(int x, int y) {
    dragging_window = -1;

    int wid = window_at_point(x, y);
    if (wid >= 0) {
        window_t *w = &windows[wid];
        if (y >= w->y + TITLE_BAR_HEIGHT) {
            int local_x = x - w->x - 1;
            int local_y = y - w->y - TITLE_BAR_HEIGHT - 1;
            push_event(wid, WIN_EVENT_MOUSE_UP, local_x, local_y, 0);
        }
    }
}

static void handle_mouse_move(int x, int y) {
    if (dragging_window >= 0) {
        window_t *w = &windows[dragging_window];
        w->x = x - drag_offset_x;
        w->y = y - drag_offset_y;

        // Clamp to screen
        if (w->x < 0) w->x = 0;
        if (w->y < MENU_BAR_HEIGHT) w->y = MENU_BAR_HEIGHT;
        if (w->x + w->w > SCREEN_WIDTH) w->x = SCREEN_WIDTH - w->w;
        if (w->y + w->h > SCREEN_HEIGHT - DOCK_HEIGHT)
            w->y = SCREEN_HEIGHT - DOCK_HEIGHT - w->h;
    }
}

static void handle_keyboard(void) {
    while (api->has_key()) {
        int c = api->getc();

        // Send to focused window
        if (focused_window >= 0) {
            push_event(focused_window, WIN_EVENT_KEY, c, 0, 0);
        }

        // Global shortcuts
        if (c == 'q' || c == 'Q') {
            // For debugging - quit desktop
            // running = 0;
        }
    }
}

// ============ Main ============

static void register_window_api(void) {
    // Register our window functions in kapi
    // This is a bit of a hack - we're modifying kapi from userspace
    // But since we're all in the same address space, it works
    api->window_create = wm_window_create;
    api->window_destroy = wm_window_destroy;
    api->window_get_buffer = wm_window_get_buffer;
    api->window_poll_event = wm_window_poll_event;
    api->window_invalidate = wm_window_invalidate;
    api->window_set_title = wm_window_set_title;
}

int main(kapi_t *kapi, int argc, char **argv) {
    (void)argc;
    (void)argv;

    api = kapi;

    // Allocate backbuffer
    backbuffer = api->malloc(SCREEN_WIDTH * SCREEN_HEIGHT * sizeof(uint32_t));
    if (!backbuffer) {
        api->puts("Desktop: failed to allocate backbuffer\n");
        return 1;
    }

    // Initialize
    init_dock_positions();
    register_window_api();

    mouse_x = 0;
    mouse_y = 0;
    mouse_prev_x = 0;
    mouse_prev_y = 0;

    // Main loop
    while (running) {
        // Poll mouse
        api->mouse_poll();
        api->mouse_get_pos(&mouse_x, &mouse_y);
        mouse_buttons = api->mouse_get_buttons();

        // Handle mouse events
        int left_pressed = (mouse_buttons & MOUSE_BTN_LEFT) && !(mouse_prev_buttons & MOUSE_BTN_LEFT);
        int left_released = !(mouse_buttons & MOUSE_BTN_LEFT) && (mouse_prev_buttons & MOUSE_BTN_LEFT);
        int right_pressed = (mouse_buttons & MOUSE_BTN_RIGHT) && !(mouse_prev_buttons & MOUSE_BTN_RIGHT);

        if (left_pressed || right_pressed) {
            uint8_t pressed_btns = 0;
            if (left_pressed) pressed_btns |= MOUSE_BTN_LEFT;
            if (right_pressed) pressed_btns |= MOUSE_BTN_RIGHT;
            handle_mouse_click(mouse_x, mouse_y, pressed_btns);
        }
        if (left_released) {
            handle_mouse_release(mouse_x, mouse_y);
        }
        if (mouse_x != mouse_prev_x || mouse_y != mouse_prev_y) {
            handle_mouse_move(mouse_x, mouse_y);
        }

        // Handle keyboard
        handle_keyboard();

        // Always redraw (simple approach - can optimize later)
        draw_desktop();
        draw_cursor(mouse_x, mouse_y);
        flip_buffer();

        mouse_prev_x = mouse_x;
        mouse_prev_y = mouse_y;
        mouse_prev_buttons = mouse_buttons;

        // Yield to other processes
        api->yield();
    }

    // Cleanup
    api->free(backbuffer);

    return 0;
}
