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
#include "../lib/gfx.h"
#include "../lib/icons.h"

// Screen dimensions (set dynamically from kapi)
static int SCREEN_WIDTH;
static int SCREEN_HEIGHT;

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
static gfx_ctx_t gfx;  // Graphics context for backbuffer
static window_t windows[MAX_WINDOWS];
static int window_order[MAX_WINDOWS];  // Z-order: window_order[0] is topmost
static int window_count = 0;
static int focused_window = -1;

// Hardware double buffering state
static int use_hw_double_buffer = 0;
static int current_buffer = 0;

// Mouse state
static int mouse_x, mouse_y;
static int mouse_prev_x, mouse_prev_y;
static uint8_t mouse_buttons;
static uint8_t mouse_prev_buttons;

// Dragging state
static int dragging_window = -1;
static int drag_offset_x, drag_offset_y;

// Resizing state
static int resizing_window = -1;
static int resize_start_w, resize_start_h;
static int resize_start_mx, resize_start_my;

// Desktop running flag
static int running = 1;

// Redraw control - skip frames when nothing changed
static int needs_redraw = 1;        // Full redraw needed
static int cursor_moved = 0;        // Just cursor position changed

// Cursor background save (for cursor-only updates)
static uint32_t cursor_save[16 * 16];
static int cursor_save_x = -100, cursor_save_y = -100;
static int cursor_save_valid = 0;

// Dock hover state (to avoid redrawing dock when nothing changed)
static int dock_hover_idx = -1;
static int dock_hover_prev = -1;

// Menu system
#define MENU_NONE   -1
#define MENU_APPLE   0
#define MENU_FILE    1
#define MENU_EDIT    2

static int open_menu = MENU_NONE;  // Currently open menu (-1 = none)

// Menu item structure
typedef struct {
    const char *label;   // NULL = separator
    int action;          // Action ID
} menu_item_t;

// Action IDs
#define ACTION_NONE           0
#define ACTION_ABOUT          1
#define ACTION_QUIT           2
#define ACTION_NEW_WINDOW     3
#define ACTION_CLOSE_WINDOW   4
#define ACTION_CUT            5
#define ACTION_COPY           6
#define ACTION_PASTE          7

// Apple menu items
static const menu_item_t apple_menu[] = {
    { "About This Computer", ACTION_ABOUT },
    { NULL, 0 },  // separator
    { "Quit Desktop", ACTION_QUIT },
    { NULL, -1 }  // end marker
};

// File menu items
static const menu_item_t file_menu[] = {
    { "New Terminal", ACTION_NEW_WINDOW },
    { "Close Window", ACTION_CLOSE_WINDOW },
    { NULL, -1 }
};

// Edit menu items
static const menu_item_t edit_menu[] = {
    { "Cut", ACTION_CUT },
    { "Copy", ACTION_COPY },
    { "Paste", ACTION_PASTE },
    { NULL, -1 }
};

// Forward declarations
static void draw_desktop(void);
static void draw_window(int wid);
static void draw_dock(void);
static void draw_menu_bar(void);
static void flip_buffer(void);
static void draw_about_dialog(void);

// Request a full screen redraw
static inline void request_redraw(void) {
    needs_redraw = 1;
}

// About dialog state (declared here so draw_desktop can see it)
static int show_about_dialog = 0;

// ============ Backbuffer Drawing (wrappers around gfx lib) ============

#define bb_put_pixel(x, y, c)           gfx_put_pixel(&gfx, x, y, c)
#define bb_fill_rect(x, y, w, h, c)     gfx_fill_rect(&gfx, x, y, w, h, c)
#define bb_draw_char(x, y, ch, fg, bg)  gfx_draw_char(&gfx, x, y, ch, fg, bg)
#define bb_draw_string(x, y, s, fg, bg) gfx_draw_string(&gfx, x, y, s, fg, bg)
#define bb_draw_hline(x, y, w, c)       gfx_draw_hline(&gfx, x, y, w, c)
#define bb_draw_vline(x, y, h, c)       gfx_draw_vline(&gfx, x, y, h, c)
#define bb_draw_rect(x, y, w, h, c)     gfx_draw_rect(&gfx, x, y, w, h, c)
#define bb_fill_pattern(x, y, w, h)     gfx_fill_pattern(&gfx, x, y, w, h, COLOR_BLACK, COLOR_WHITE)

// ============ VibeOS Logo (from icons.h) ============

static void draw_vibeos_logo(int x, int y) {
    for (int py = 0; py < 16; py++) {
        for (int px = 0; px < 16; px++) {
            if (vibeos_logo[py * 16 + px]) {
                bb_put_pixel(x + px, y + py, COLOR_BLACK);
            }
        }
    }
}

// ============ Dock Icons (from icons.h) ============

static void draw_icon_bitmap(int x, int y, const unsigned char *bitmap, int inverted) {
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
    request_redraw();
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

    // Clear to white (use DMA if available for speed)
    if (api->dma_fill) {
        api->dma_fill(win->buffer, COLOR_WIN_BG, w * content_h * sizeof(uint32_t));
    } else {
        for (int j = 0; j < w * content_h; j++) {
            win->buffer[j] = COLOR_WIN_BG;
        }
    }

    // Add to z-order (at front)
    for (int j = window_count; j > 0; j--) {
        window_order[j] = window_order[j - 1];
    }
    window_order[0] = wid;
    window_count++;
    focused_window = wid;
    request_redraw();

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
    request_redraw();
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
    request_redraw();
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
    request_redraw();
}

// ============ Dock ============

#define DOCK_ICON_SIZE 32
#define DOCK_PADDING 12
#define DOCK_LABEL_HEIGHT 12

// Dock icons with bitmap indices
static dock_icon_t dock_icons[] = {
    { 0, 0, DOCK_ICON_SIZE, DOCK_ICON_SIZE, "Snake",   "/bin/snake",   1 },
    { 0, 0, DOCK_ICON_SIZE, DOCK_ICON_SIZE, "Tetris",  "/bin/tetris",  1 },
    { 0, 0, DOCK_ICON_SIZE, DOCK_ICON_SIZE, "Calc",    "/bin/calc",    0 },
    { 0, 0, DOCK_ICON_SIZE, DOCK_ICON_SIZE, "Files",   "/bin/files",   0 },
    { 0, 0, DOCK_ICON_SIZE, DOCK_ICON_SIZE, "Music",   "/bin/music",   0 },
    { 0, 0, DOCK_ICON_SIZE, DOCK_ICON_SIZE, "Browser", "/bin/browser", 0 },
    { 0, 0, DOCK_ICON_SIZE, DOCK_ICON_SIZE, "Term",    "/bin/term",    0 },
    { 0, 0, DOCK_ICON_SIZE, DOCK_ICON_SIZE, "SysMon",  "/bin/sysmon",  0 },
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

// Format time string: "HH:MM" (5 chars + null)
static void format_time(char *buf) {
    int year, month, day, hour, minute, second, weekday;
    api->get_datetime(&year, &month, &day, &hour, &minute, &second, &weekday);

    buf[0] = '0' + (hour / 10);
    buf[1] = '0' + (hour % 10);
    buf[2] = ':';
    buf[3] = '0' + (minute / 10);
    buf[4] = '0' + (minute % 10);
    buf[5] = '\0';
}

// Format date string: "Mon Dec 8" (max ~10 chars)
static void format_date(char *buf) {
    static const char day_names[7][4] = {"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};
    static const char month_names[12][4] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun",
                                             "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};

    int year, month, day, hour, minute, second, weekday;
    (void)year; (void)hour; (void)minute; (void)second;
    api->get_datetime(&year, &month, &day, &hour, &minute, &second, &weekday);

    // Copy day name
    buf[0] = day_names[weekday][0];
    buf[1] = day_names[weekday][1];
    buf[2] = day_names[weekday][2];
    buf[3] = ' ';

    // Copy month name
    buf[4] = month_names[month - 1][0];
    buf[5] = month_names[month - 1][1];
    buf[6] = month_names[month - 1][2];
    buf[7] = ' ';

    // Day number
    if (day >= 10) {
        buf[8] = '0' + (day / 10);
        buf[9] = '0' + (day % 10);
        buf[10] = '\0';
    } else {
        buf[8] = '0' + day;
        buf[9] = '\0';
    }
}

// Menu bar item positions (x, width)
#define APPLE_MENU_X     4
#define APPLE_MENU_W     20
#define FILE_MENU_X      28
#define FILE_MENU_W      32
#define EDIT_MENU_X      68
#define EDIT_MENU_W      32

static void draw_dropdown_menu(int menu_x, const menu_item_t *items) {
    // Calculate menu dimensions
    int max_width = 0;
    int item_count = 0;
    for (int i = 0; items[i].action != -1; i++) {
        if (items[i].label) {
            int len = strlen(items[i].label);
            if (len > max_width) max_width = len;
        }
        item_count++;
    }

    int menu_w = max_width * 8 + 24;  // Padding on sides
    int menu_h = item_count * 16 + 4; // 16px per item + border

    int menu_y = MENU_BAR_HEIGHT;

    // Draw menu background with shadow
    bb_fill_rect(menu_x + 2, menu_y + 2, menu_w, menu_h, COLOR_BLACK);  // Shadow
    bb_fill_rect(menu_x, menu_y, menu_w, menu_h, COLOR_WHITE);
    bb_draw_rect(menu_x, menu_y, menu_w, menu_h, COLOR_BLACK);

    // Draw items
    int y = menu_y + 2;
    for (int i = 0; items[i].action != -1; i++) {
        if (items[i].label == NULL) {
            // Separator
            bb_draw_hline(menu_x + 4, y + 7, menu_w - 8, COLOR_BLACK);
        } else {
            // Check if mouse is over this item
            int item_y = y;
            int hovering = (mouse_y >= item_y && mouse_y < item_y + 16 &&
                           mouse_x >= menu_x && mouse_x < menu_x + menu_w);

            if (hovering) {
                bb_fill_rect(menu_x + 2, item_y, menu_w - 4, 16, COLOR_BLACK);
                bb_draw_string(menu_x + 12, item_y + 1, items[i].label, COLOR_WHITE, COLOR_BLACK);
            } else {
                bb_draw_string(menu_x + 12, item_y + 1, items[i].label, COLOR_BLACK, COLOR_WHITE);
            }
        }
        y += 16;
    }
}

static void draw_menu_bar(void) {
    // Background
    bb_fill_rect(0, 0, SCREEN_WIDTH, MENU_BAR_HEIGHT, COLOR_MENU_BG);
    // Bottom border - double line for 3D effect
    bb_draw_hline(0, MENU_BAR_HEIGHT - 2, SCREEN_WIDTH, COLOR_BLACK);
    bb_draw_hline(0, MENU_BAR_HEIGHT - 1, SCREEN_WIDTH, COLOR_BLACK);

    // VibeOS logo in menu bar (highlighted if menu open)
    if (open_menu == MENU_APPLE) {
        bb_fill_rect(APPLE_MENU_X - 2, 0, APPLE_MENU_W + 4, MENU_BAR_HEIGHT - 2, COLOR_BLACK);
        // Draw inverted logo
        for (int py = 0; py < 16; py++) {
            for (int px = 0; px < 16; px++) {
                if (vibeos_logo[py * 16 + px]) {
                    bb_put_pixel(APPLE_MENU_X + px, 2 + py, COLOR_WHITE);
                }
            }
        }
    } else {
        draw_vibeos_logo(APPLE_MENU_X, 2);
    }

    // File menu
    if (open_menu == MENU_FILE) {
        bb_fill_rect(FILE_MENU_X - 4, 0, FILE_MENU_W + 8, MENU_BAR_HEIGHT - 2, COLOR_BLACK);
        bb_draw_string(FILE_MENU_X, 2, "File", COLOR_WHITE, COLOR_BLACK);
    } else {
        bb_draw_string(FILE_MENU_X, 2, "File", COLOR_MENU_TEXT, COLOR_MENU_BG);
    }

    // Edit menu
    if (open_menu == MENU_EDIT) {
        bb_fill_rect(EDIT_MENU_X - 4, 0, EDIT_MENU_W + 8, MENU_BAR_HEIGHT - 2, COLOR_BLACK);
        bb_draw_string(EDIT_MENU_X, 2, "Edit", COLOR_WHITE, COLOR_BLACK);
    } else {
        bb_draw_string(EDIT_MENU_X, 2, "Edit", COLOR_MENU_TEXT, COLOR_MENU_BG);
    }

    // Date and time on right side
    char date_buf[16];
    char time_buf[8];
    format_date(date_buf);
    format_time(time_buf);

    // Draw date then time: "Mon Dec 8  12:00"
    int date_len = strlen(date_buf);
    int time_x = SCREEN_WIDTH - 48;  // Time on far right
    int date_x = time_x - (date_len * 8) - 16;  // Date with gap

    bb_draw_string(date_x, 2, date_buf, COLOR_MENU_TEXT, COLOR_MENU_BG);
    bb_draw_string(time_x, 2, time_buf, COLOR_MENU_TEXT, COLOR_MENU_BG);
}

static void draw_open_menu(void) {
    if (open_menu == MENU_APPLE) {
        draw_dropdown_menu(APPLE_MENU_X - 2, apple_menu);
    } else if (open_menu == MENU_FILE) {
        draw_dropdown_menu(FILE_MENU_X - 4, file_menu);
    } else if (open_menu == MENU_EDIT) {
        draw_dropdown_menu(EDIT_MENU_X - 4, edit_menu);
    }
}

// ============ Window Drawing ============

// Draw System 7 style horizontal stripes for title bar (optimized)
static void draw_title_stripes(int x, int y, int w, int h) {
    for (int row = 0; row < h; row++) {
        // Every other row is black (creates stripe effect)
        // Use fast horizontal line drawing instead of per-pixel
        uint32_t color = (row & 1) ? COLOR_BLACK : COLOR_WHITE;
        bb_draw_hline(x, y + row, w, color);
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

    // Check if window is fully on screen (no clipping needed)
    int fully_visible = (w->x + 2 >= 0) &&
                        (w->x + 2 + content_w <= SCREEN_WIDTH) &&
                        (content_y >= 0) &&
                        (content_y + content_h <= SCREEN_HEIGHT);

    if (fully_visible && api->dma_available && api->dma_available() && content_w > 0 && content_h > 0) {
        // Use DMA 2D copy for fast rectangular blit
        uint32_t *dst = &backbuffer[content_y * SCREEN_WIDTH + w->x + 2];
        uint32_t dst_pitch = SCREEN_WIDTH * sizeof(uint32_t);
        uint32_t *src = w->buffer;
        uint32_t src_pitch = w->w * sizeof(uint32_t);
        uint32_t copy_bytes = content_w * sizeof(uint32_t);

        api->dma_copy_2d(dst, dst_pitch, src, src_pitch, copy_bytes, content_h);
    } else {
        // Fallback: row-wise copy with clipping
        for (int py = 0; py < content_h; py++) {
            int screen_y = content_y + py;
            if (screen_y >= SCREEN_HEIGHT) break;

            int dst_offset = screen_y * SCREEN_WIDTH + w->x + 2;
            int src_offset = py * w->w;

            // Clip width to screen bounds
            int copy_w = content_w;
            if (w->x + 2 + copy_w > SCREEN_WIDTH) {
                copy_w = SCREEN_WIDTH - (w->x + 2);
            }
            if (copy_w > 0) {
                // Use 64-bit copy for entire row
                memcpy64(&backbuffer[dst_offset], &w->buffer[src_offset], copy_w * sizeof(uint32_t));
            }
        }
    }

    // Resize handle (bottom-right corner) - System 7 style diagonal lines
    int rh_x = w->x + w->w - 15;
    int rh_y = w->y + w->h - 15;
    // Draw 3 diagonal lines
    for (int i = 0; i < 3; i++) {
        int offset = i * 4;
        // Each line is 2 pixels thick for visibility
        for (int d = 0; d < 10 - offset; d++) {
            bb_put_pixel(rh_x + offset + d + 4, rh_y + d + 4, COLOR_BLACK);
        }
    }
}

// ============ Cursor ============

// Save the background under cursor position from a buffer
static void save_cursor_bg(uint32_t *buffer, int x, int y) {
    for (int py = 0; py < 16; py++) {
        for (int px = 0; px < 16; px++) {
            int sx = x + px, sy = y + py;
            if (sx >= 0 && sx < SCREEN_WIDTH && sy >= 0 && sy < SCREEN_HEIGHT) {
                cursor_save[py * 16 + px] = buffer[sy * SCREEN_WIDTH + sx];
            } else {
                cursor_save[py * 16 + px] = COLOR_BLACK;
            }
        }
    }
    cursor_save_x = x;
    cursor_save_y = y;
    cursor_save_valid = 1;
}

// Restore cursor background to a buffer
static void restore_cursor_bg(uint32_t *buffer) {
    if (!cursor_save_valid) return;
    for (int py = 0; py < 16; py++) {
        for (int px = 0; px < 16; px++) {
            int sx = cursor_save_x + px, sy = cursor_save_y + py;
            if (sx >= 0 && sx < SCREEN_WIDTH && sy >= 0 && sy < SCREEN_HEIGHT) {
                buffer[sy * SCREEN_WIDTH + sx] = cursor_save[py * 16 + px];
            }
        }
    }
}

// Draw cursor to a specific buffer
static void draw_cursor_to_buffer(uint32_t *buffer, int x, int y) {
    // Classic Mac-style arrow cursor
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
                    buffer[sy * SCREEN_WIDTH + sx] = color;
                }
            }
        }
    }
}

// Get pointer to the currently visible buffer
static uint32_t *get_visible_buffer(void) {
    if (use_hw_double_buffer) {
        // After flip_buffer(), current_buffer was toggled and now points to the BACKBUFFER
        // So the VISIBLE buffer is the opposite of current_buffer
        // If current_buffer == 0, visible = buffer 1 (bottom half)
        // If current_buffer == 1, visible = buffer 0 (top half)
        return api->fb_base + (current_buffer ? 0 : SCREEN_WIDTH * SCREEN_HEIGHT);
    } else {
        return api->fb_base;
    }
}

// Update cursor position on the visible buffer (for cursor-only updates)
static void update_cursor_only(int old_x, int old_y, int new_x, int new_y) {
    uint32_t *visible = get_visible_buffer();
    (void)old_x; (void)old_y;  // We use cursor_save_x/y instead

    // Restore old cursor background
    restore_cursor_bg(visible);

    // Save new cursor background
    save_cursor_bg(visible, new_x, new_y);

    // Draw cursor at new position
    draw_cursor_to_buffer(visible, new_x, new_y);
}

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

    // Open menu dropdown (draw last, on top of everything)
    if (open_menu != MENU_NONE) {
        draw_open_menu();
    }

    // About dialog (on top of everything)
    if (show_about_dialog) {
        draw_about_dialog();
    }
}

static void flip_buffer(void) {
    if (use_hw_double_buffer) {
        // Hardware flip - instant, zero-copy!
        // We've been drawing to the backbuffer (non-visible half)
        // Now make it visible by switching scroll offset
        api->fb_flip(current_buffer);
        current_buffer = !current_buffer;
        // Update backbuffer pointer to the now-hidden buffer
        backbuffer = api->fb_get_backbuffer();
        gfx.buffer = backbuffer;
    } else if (api->dma_available && api->dma_available()) {
        // DMA copy - hardware accelerated, frees CPU (Pi)
        api->dma_fb_copy(api->fb_base, backbuffer, SCREEN_WIDTH, SCREEN_HEIGHT);
    } else {
        // Software copy (QEMU fallback) - use fast 64-bit copy
        memcpy64(api->fb_base, backbuffer, SCREEN_WIDTH * SCREEN_HEIGHT * sizeof(uint32_t));
    }
}

// ============ Input Handling ============

#define ABOUT_W 280
#define ABOUT_H 180
#define ABOUT_X ((SCREEN_WIDTH - ABOUT_W) / 2)
#define ABOUT_Y ((SCREEN_HEIGHT - ABOUT_H) / 2 - 20)

static void draw_about_dialog(void) {
    int x = ABOUT_X;
    int y = ABOUT_Y;

    // Shadow
    bb_fill_rect(x + 3, y + 3, ABOUT_W, ABOUT_H, COLOR_BLACK);

    // Background
    bb_fill_rect(x, y, ABOUT_W, ABOUT_H, COLOR_WHITE);

    // Border (double line)
    bb_draw_rect(x, y, ABOUT_W, ABOUT_H, COLOR_BLACK);
    bb_draw_rect(x + 1, y + 1, ABOUT_W - 2, ABOUT_H - 2, COLOR_BLACK);

    // Draw a big VibeOS logo in the dialog (2x size)
    int logo_x = x + (ABOUT_W - 32) / 2;  // 16*2 = 32
    int logo_y = y + 12;
    for (int py = 0; py < 16; py++) {
        for (int px = 0; px < 16; px++) {
            if (vibeos_logo[py * 16 + px]) {
                // Draw it 2x size
                bb_put_pixel(logo_x + px*2, logo_y + py*2, COLOR_BLACK);
                bb_put_pixel(logo_x + px*2 + 1, logo_y + py*2, COLOR_BLACK);
                bb_put_pixel(logo_x + px*2, logo_y + py*2 + 1, COLOR_BLACK);
                bb_put_pixel(logo_x + px*2 + 1, logo_y + py*2 + 1, COLOR_BLACK);
            }
        }
    }

    // Title
    const char *title = "VibeOS";
    int title_x = x + (ABOUT_W - strlen(title) * 8) / 2;
    bb_draw_string(title_x, y + 50, title, COLOR_BLACK, COLOR_WHITE);

    // Version
    const char *version = "Version 1.0";
    int ver_x = x + (ABOUT_W - strlen(version) * 8) / 2;
    bb_draw_string(ver_x, y + 68, version, COLOR_BLACK, COLOR_WHITE);

    // Separator line
    bb_draw_hline(x + 20, y + 88, ABOUT_W - 40, COLOR_BLACK);

    // System info
    // Memory
    unsigned long mem_used = api->get_mem_used() / 1024;  // KB
    unsigned long mem_free = api->get_mem_free() / 1024;  // KB
    unsigned long mem_total = mem_used + mem_free;

    char mem_str[40];
    // Manual sprintf: "Memory: XXX KB used / XXX KB total"
    char *p = mem_str;
    const char *m1 = "Memory: ";
    while (*m1) *p++ = *m1++;

    // Used KB
    char num[12];
    int ni = 0;
    unsigned long n = mem_used;
    if (n == 0) num[ni++] = '0';
    else { while (n > 0) { num[ni++] = '0' + (n % 10); n /= 10; } }
    while (ni > 0) *p++ = num[--ni];

    const char *m2 = " / ";
    while (*m2) *p++ = *m2++;

    // Total KB
    n = mem_total;
    ni = 0;
    if (n == 0) num[ni++] = '0';
    else { while (n > 0) { num[ni++] = '0' + (n % 10); n /= 10; } }
    while (ni > 0) *p++ = num[--ni];

    const char *m3 = " KB";
    while (*m3) *p++ = *m3++;
    *p = '\0';

    int mem_x = x + (ABOUT_W - strlen(mem_str) * 8) / 2;
    bb_draw_string(mem_x, y + 100, mem_str, COLOR_BLACK, COLOR_WHITE);

    // Uptime
    unsigned long ticks = api->get_uptime_ticks();
    unsigned long secs = ticks / 100;
    unsigned long mins = secs / 60;
    unsigned long hours = mins / 60;
    mins = mins % 60;
    secs = secs % 60;

    char up_str[32];
    p = up_str;
    const char *u1 = "Uptime: ";
    while (*u1) *p++ = *u1++;

    // Hours
    n = hours;
    ni = 0;
    if (n == 0) num[ni++] = '0';
    else { while (n > 0) { num[ni++] = '0' + (n % 10); n /= 10; } }
    while (ni > 0) *p++ = num[--ni];
    *p++ = ':';

    // Minutes (2 digits)
    *p++ = '0' + (mins / 10);
    *p++ = '0' + (mins % 10);
    *p++ = ':';

    // Seconds (2 digits)
    *p++ = '0' + (secs / 10);
    *p++ = '0' + (secs % 10);
    *p = '\0';

    int up_x = x + (ABOUT_W - strlen(up_str) * 8) / 2;
    bb_draw_string(up_x, y + 118, up_str, COLOR_BLACK, COLOR_WHITE);

    // OK button
    int btn_w = 60;
    int btn_h = 20;
    int btn_x = x + (ABOUT_W - btn_w) / 2;
    int btn_y = y + ABOUT_H - 35;

    // Check if hovering over button
    int hovering = (mouse_x >= btn_x && mouse_x < btn_x + btn_w &&
                   mouse_y >= btn_y && mouse_y < btn_y + btn_h);

    if (hovering) {
        bb_fill_rect(btn_x, btn_y, btn_w, btn_h, COLOR_BLACK);
        bb_draw_string(btn_x + 20, btn_y + 3, "OK", COLOR_WHITE, COLOR_BLACK);
    } else {
        bb_fill_rect(btn_x, btn_y, btn_w, btn_h, COLOR_WHITE);
        bb_draw_rect(btn_x, btn_y, btn_w, btn_h, COLOR_BLACK);
        bb_draw_rect(btn_x + 2, btn_y + 2, btn_w - 4, btn_h - 4, COLOR_BLACK);
        bb_draw_string(btn_x + 20, btn_y + 3, "OK", COLOR_BLACK, COLOR_WHITE);
    }
}

// Execute a menu action
static void do_menu_action(int action) {
    switch (action) {
        case ACTION_ABOUT:
            show_about_dialog = 1;
            request_redraw();
            break;
        case ACTION_QUIT:
            running = 0;
            break;
        case ACTION_NEW_WINDOW:
            api->spawn("/bin/term");
            break;
        case ACTION_CLOSE_WINDOW:
            if (focused_window >= 0) {
                push_event(focused_window, WIN_EVENT_CLOSE, 0, 0, 0);
            }
            break;
        case ACTION_CUT:
        case ACTION_COPY:
        case ACTION_PASTE:
            // TODO: Clipboard operations
            break;
    }
}

// Check if click is on a menu item and return its action
static int get_menu_item_action(int menu_x, const menu_item_t *items, int click_x, int click_y) {
    // Calculate menu dimensions
    int max_width = 0;
    int item_count = 0;
    for (int i = 0; items[i].action != -1; i++) {
        if (items[i].label) {
            int len = strlen(items[i].label);
            if (len > max_width) max_width = len;
        }
        item_count++;
    }

    int menu_w = max_width * 8 + 24;
    int menu_y = MENU_BAR_HEIGHT;

    // Check if click is within menu bounds
    if (click_x < menu_x || click_x >= menu_x + menu_w) return ACTION_NONE;
    if (click_y < menu_y) return ACTION_NONE;

    // Find which item was clicked
    int y = menu_y + 2;
    for (int i = 0; items[i].action != -1; i++) {
        if (click_y >= y && click_y < y + 16) {
            if (items[i].label != NULL) {
                return items[i].action;
            }
            return ACTION_NONE;  // Clicked on separator
        }
        y += 16;
    }

    return ACTION_NONE;
}

static void handle_mouse_click(int x, int y, uint8_t buttons) {
    // Handle About dialog (modal - blocks everything else)
    if (show_about_dialog && (buttons & MOUSE_BTN_LEFT)) {
        // Check OK button
        int btn_w = 60;
        int btn_h = 20;
        int btn_x = ABOUT_X + (ABOUT_W - btn_w) / 2;
        int btn_y = ABOUT_Y + ABOUT_H - 35;

        if (x >= btn_x && x < btn_x + btn_w &&
            y >= btn_y && y < btn_y + btn_h) {
            show_about_dialog = 0;
            request_redraw();
        }
        // Click anywhere in dialog dismisses it too
        if (x >= ABOUT_X && x < ABOUT_X + ABOUT_W &&
            y >= ABOUT_Y && y < ABOUT_Y + ABOUT_H) {
            // Clicked inside dialog, but not button - do nothing (or dismiss)
        } else {
            // Clicked outside dialog - dismiss it
            show_about_dialog = 0;
            request_redraw();
        }
        return;  // Modal - don't process other clicks
    }

    // Handle menu bar clicks (left click only)
    if ((buttons & MOUSE_BTN_LEFT) && y < MENU_BAR_HEIGHT) {
        // Check which menu was clicked
        if (x >= APPLE_MENU_X && x < APPLE_MENU_X + APPLE_MENU_W) {
            open_menu = (open_menu == MENU_APPLE) ? MENU_NONE : MENU_APPLE;
        } else if (x >= FILE_MENU_X && x < FILE_MENU_X + FILE_MENU_W) {
            open_menu = (open_menu == MENU_FILE) ? MENU_NONE : MENU_FILE;
        } else if (x >= EDIT_MENU_X && x < EDIT_MENU_X + EDIT_MENU_W) {
            open_menu = (open_menu == MENU_EDIT) ? MENU_NONE : MENU_EDIT;
        } else {
            open_menu = MENU_NONE;
        }
        request_redraw();
        return;
    }

    // Handle clicks on open menu dropdown
    if ((buttons & MOUSE_BTN_LEFT) && open_menu != MENU_NONE) {
        int action = ACTION_NONE;

        if (open_menu == MENU_APPLE) {
            action = get_menu_item_action(APPLE_MENU_X - 2, apple_menu, x, y);
        } else if (open_menu == MENU_FILE) {
            action = get_menu_item_action(FILE_MENU_X - 4, file_menu, x, y);
        } else if (open_menu == MENU_EDIT) {
            action = get_menu_item_action(EDIT_MENU_X - 4, edit_menu, x, y);
        }

        if (action != ACTION_NONE) {
            do_menu_action(action);
        }

        // Close menu after any click outside menu bar
        open_menu = MENU_NONE;
        request_redraw();
        return;
    }

    // Check dock first (left click only)
    if (buttons & MOUSE_BTN_LEFT) {
        int dock_idx = dock_icon_at_point(x, y);
        if (dock_idx >= 0) {
            dock_icon_t *icon = &dock_icons[dock_idx];
            if (icon->is_fullscreen) {
                // Fullscreen app - exec and wait
                api->exec(icon->exec_path);
                // When we return, redraw everything
                request_redraw();
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
            // Check for resize handle (bottom-right corner, 15x15 area)
            int rh_x = w->x + w->w - 15;
            int rh_y = w->y + w->h - 15;
            if ((buttons & MOUSE_BTN_LEFT) &&
                x >= rh_x && x < w->x + w->w &&
                y >= rh_y && y < w->y + w->h) {
                // Start resizing
                resizing_window = wid;
                resize_start_w = w->w;
                resize_start_h = w->h;
                resize_start_mx = x;
                resize_start_my = y;
                return;
            }
            // Click in content area - send event to app with button info
            int local_x = x - w->x - 1;
            int local_y = y - w->y - TITLE_BAR_HEIGHT - 1;
            push_event(wid, WIN_EVENT_MOUSE_DOWN, local_x, local_y, buttons);
        }
    }
}

static void handle_mouse_release(int x, int y) {
    dragging_window = -1;

    // Handle resize completion
    if (resizing_window >= 0) {
        window_t *w = &windows[resizing_window];

        // Allocate new buffer before freeing old one (in case malloc fails)
        int content_h = w->h - TITLE_BAR_HEIGHT;
        if (content_h < 1) content_h = 1;
        uint32_t *new_buffer = api->malloc(w->w * content_h * sizeof(uint32_t));

        if (new_buffer) {
            // Success - free old buffer and use new one
            api->free(w->buffer);
            w->buffer = new_buffer;

            // Clear new buffer to white (use DMA if available)
            if (api->dma_fill) {
                api->dma_fill(w->buffer, COLOR_WHITE, w->w * content_h * sizeof(uint32_t));
            } else {
                for (int i = 0; i < w->w * content_h; i++) {
                    w->buffer[i] = COLOR_WHITE;
                }
            }

            // Send resize event to app (data1=new width, data2=new height)
            push_event(resizing_window, WIN_EVENT_RESIZE, w->w, w->h, 0);
        }
        // If malloc failed, keep old buffer and size (resize is cancelled)

        resizing_window = -1;
        request_redraw();
        return;
    }

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
        request_redraw();
        return;  // Don't send move events while dragging
    }

    if (resizing_window >= 0) {
        window_t *w = &windows[resizing_window];

        // Calculate new size based on mouse delta
        int new_w = resize_start_w + (x - resize_start_mx);
        int new_h = resize_start_h + (y - resize_start_my);

        // Enforce minimum size
        if (new_w < 100) new_w = 100;
        if (new_h < 60) new_h = 60;

        // Enforce maximum (screen bounds)
        if (w->x + new_w > SCREEN_WIDTH) new_w = SCREEN_WIDTH - w->x;
        if (w->y + new_h > SCREEN_HEIGHT - DOCK_HEIGHT)
            new_h = SCREEN_HEIGHT - DOCK_HEIGHT - w->y;

        w->w = new_w;
        w->h = new_h;
        request_redraw();
        return;  // Don't send move events while resizing
    }

    // Send mouse move event to window under cursor (if in content area)
    int wid = window_at_point(x, y);
    if (wid >= 0) {
        window_t *w = &windows[wid];
        // Only send if in content area (below title bar)
        if (y >= w->y + TITLE_BAR_HEIGHT) {
            int local_x = x - w->x - 1;
            int local_y = y - w->y - TITLE_BAR_HEIGHT - 1;
            uint8_t buttons = api->mouse_get_buttons();
            push_event(wid, WIN_EVENT_MOUSE_MOVE, local_x, local_y, buttons);
        }
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

    // Get screen dimensions from kapi
    SCREEN_WIDTH = api->fb_width;
    SCREEN_HEIGHT = api->fb_height;

    // Check for hardware double buffering (Pi only)
    if (api->fb_has_hw_double_buffer && api->fb_has_hw_double_buffer()) {
        use_hw_double_buffer = 1;
        // Use the kernel-provided backbuffer (part of the 2x height framebuffer)
        backbuffer = api->fb_get_backbuffer();
        // Determine which buffer we're drawing to based on backbuffer address
        // If backbuffer is the bottom half, we'll flip to show buffer 1
        // If backbuffer is the top half, we'll flip to show buffer 0
        if (backbuffer == api->fb_base) {
            current_buffer = 0;  // Drawing to top, will show top
        } else {
            current_buffer = 1;  // Drawing to bottom, will show bottom
        }
        api->puts("Desktop: using hardware double buffering\n");
    } else {
        use_hw_double_buffer = 0;
        // Allocate our own backbuffer
        backbuffer = api->malloc(SCREEN_WIDTH * SCREEN_HEIGHT * sizeof(uint32_t));
        if (!backbuffer) {
            api->puts("Desktop: failed to allocate backbuffer\n");
            return 1;
        }
    }

    // Initialize graphics context
    gfx_init(&gfx, backbuffer, SCREEN_WIDTH, SCREEN_HEIGHT, api->font_data);

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

        // Track if cursor moved (for cursor-only updates)
        cursor_moved = (mouse_x != mouse_prev_x || mouse_y != mouse_prev_y);

        // Track dock hover state
        dock_hover_prev = dock_hover_idx;
        dock_hover_idx = dock_icon_at_point(mouse_x, mouse_y);
        int dock_hover_changed = (dock_hover_idx != dock_hover_prev);

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
        if (cursor_moved) {
            handle_mouse_move(mouse_x, mouse_y);
        }

        // Handle keyboard
        handle_keyboard();

        // Dock hover change requires full redraw (icon highlight changes)
        if (dock_hover_changed) {
            needs_redraw = 1;
        }

        // Menu open requires full redraw on cursor move (hover highlighting)
        if (open_menu != MENU_NONE && cursor_moved) {
            needs_redraw = 1;
        }

        // About dialog hover requires full redraw (button highlighting)
        if (show_about_dialog && cursor_moved) {
            needs_redraw = 1;
        }

        // Decide what to redraw
        if (needs_redraw) {
            // Full redraw needed
            draw_desktop();
            // Save cursor background BEFORE drawing cursor (so we save the clean background)
            save_cursor_bg(backbuffer, mouse_x, mouse_y);
            draw_cursor(mouse_x, mouse_y);
            flip_buffer();
            needs_redraw = 0;
        } else if (cursor_moved) {
            // Only cursor moved - update cursor directly on visible buffer
            // This is MUCH faster than a full redraw
            update_cursor_only(mouse_prev_x, mouse_prev_y, mouse_x, mouse_y);
        }

        mouse_prev_x = mouse_x;
        mouse_prev_y = mouse_y;
        mouse_prev_buttons = mouse_buttons;

        // Yield to other processes (kernel WFIs if nothing else to run)
        api->yield();
    }

    // Cleanup - clear screen to black and restore console (use DMA if available)
    if (api->dma_fill) {
        api->dma_fill(api->fb_base, COLOR_BLACK, SCREEN_WIDTH * SCREEN_HEIGHT * sizeof(uint32_t));
    } else {
        for (int i = 0; i < SCREEN_WIDTH * SCREEN_HEIGHT; i++) {
            api->fb_base[i] = COLOR_BLACK;
        }
    }

    // Reset scroll offset if using hardware double buffering
    if (use_hw_double_buffer) {
        api->fb_flip(0);  // Show top buffer
    }

    // Clear console and show exit message
    api->clear();
    api->puts("Desktop exited.\n");

    // Only free if we allocated it ourselves
    if (!use_hw_double_buffer) {
        api->free(backbuffer);
    }

    return 0;
}
