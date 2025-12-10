/*
 * VibeOS System Monitor
 *
 * Classic Mac-style system monitor showing system stats.
 * Shows: uptime, date/time, memory, disk, processes, sound status.
 */

#include "../lib/vibe.h"
#include "../lib/gfx.h"

static kapi_t *api;
static int window_id = -1;
static uint32_t *win_buffer;
static int win_w, win_h;
static gfx_ctx_t gfx;

// Window content dimensions
#define CONTENT_W 320
#define CONTENT_H 520

// Process states (must match kernel)
#define PROC_STATE_FREE    0
#define PROC_STATE_READY   1
#define PROC_STATE_RUNNING 2
#define PROC_STATE_BLOCKED 3
#define PROC_STATE_ZOMBIE  4

#define MAX_PROCESSES 16

// Drawing macros
#define buf_fill_rect(x, y, w, h, c)     gfx_fill_rect(&gfx, x, y, w, h, c)
#define buf_draw_char(x, y, ch, fg, bg)  gfx_draw_char(&gfx, x, y, ch, fg, bg)
#define buf_draw_string(x, y, s, fg, bg) gfx_draw_string(&gfx, x, y, s, fg, bg)
#define buf_draw_rect(x, y, w, h, c)     gfx_draw_rect(&gfx, x, y, w, h, c)

// ============ Formatting Helpers ============

static void format_num(char *buf, unsigned long n) {
    if (n == 0) {
        buf[0] = '0';
        buf[1] = '\0';
        return;
    }
    char tmp[20];
    int i = 0;
    while (n > 0) {
        tmp[i++] = '0' + (n % 10);
        n /= 10;
    }
    int j = 0;
    while (i > 0) {
        buf[j++] = tmp[--i];
    }
    buf[j] = '\0';
}

static void format_hex(char *buf, uint64_t n) {
    const char *hex = "0123456789ABCDEF";
    buf[0] = '0';
    buf[1] = 'x';
    for (int i = 0; i < 8; i++) {
        buf[2 + i] = hex[(n >> (28 - i * 4)) & 0xF];
    }
    buf[10] = '\0';
}

static void format_size_mb(char *buf, unsigned long bytes) {
    unsigned long mb = bytes / (1024 * 1024);
    unsigned long remainder = (bytes % (1024 * 1024)) * 10 / (1024 * 1024);

    format_num(buf, mb);
    int len = strlen(buf);
    buf[len] = '.';
    buf[len+1] = '0' + remainder;
    buf[len+2] = ' ';
    buf[len+3] = 'M';
    buf[len+4] = 'B';
    buf[len+5] = '\0';
}

static void format_size_kb(char *buf, int kb) {
    if (kb >= 1024) {
        // Show as MB
        int mb = kb / 1024;
        int remainder = ((kb % 1024) * 10) / 1024;
        format_num(buf, mb);
        int len = strlen(buf);
        buf[len] = '.';
        buf[len+1] = '0' + remainder;
        buf[len+2] = ' ';
        buf[len+3] = 'M';
        buf[len+4] = 'B';
        buf[len+5] = '\0';
    } else {
        format_num(buf, kb);
        int len = strlen(buf);
        buf[len] = ' ';
        buf[len+1] = 'K';
        buf[len+2] = 'B';
        buf[len+3] = '\0';
    }
}

static void format_uptime(char *buf, unsigned long ticks) {
    unsigned long total_seconds = ticks / 100;
    unsigned long hours = total_seconds / 3600;
    unsigned long minutes = (total_seconds % 3600) / 60;
    unsigned long seconds = total_seconds % 60;

    char tmp[8];
    int pos = 0;

    if (hours > 0) {
        format_num(tmp, hours);
        for (int i = 0; tmp[i]; i++) buf[pos++] = tmp[i];
        buf[pos++] = 'h';
        buf[pos++] = ' ';
    }

    format_num(tmp, minutes);
    for (int i = 0; tmp[i]; i++) buf[pos++] = tmp[i];
    buf[pos++] = 'm';
    buf[pos++] = ' ';

    format_num(tmp, seconds);
    for (int i = 0; tmp[i]; i++) buf[pos++] = tmp[i];
    buf[pos++] = 's';
    buf[pos] = '\0';
}

static void format_datetime(char *buf, int year, int month, int day,
                            int hour, int minute, int second) {
    // Format: YYYY-MM-DD HH:MM:SS
    int pos = 0;

    // Year
    buf[pos++] = '0' + (year / 1000) % 10;
    buf[pos++] = '0' + (year / 100) % 10;
    buf[pos++] = '0' + (year / 10) % 10;
    buf[pos++] = '0' + year % 10;
    buf[pos++] = '-';

    // Month
    buf[pos++] = '0' + (month / 10) % 10;
    buf[pos++] = '0' + month % 10;
    buf[pos++] = '-';

    // Day
    buf[pos++] = '0' + (day / 10) % 10;
    buf[pos++] = '0' + day % 10;
    buf[pos++] = ' ';

    // Hour
    buf[pos++] = '0' + (hour / 10) % 10;
    buf[pos++] = '0' + hour % 10;
    buf[pos++] = ':';

    // Minute
    buf[pos++] = '0' + (minute / 10) % 10;
    buf[pos++] = '0' + minute % 10;
    buf[pos++] = ':';

    // Second
    buf[pos++] = '0' + (second / 10) % 10;
    buf[pos++] = '0' + second % 10;

    buf[pos] = '\0';
}

// ============ Drawing ============

static void draw_progress_bar(int x, int y, int w, int h, int percent) {
    // Background
    buf_fill_rect(x, y, w, h, COLOR_WHITE);
    buf_draw_rect(x, y, w, h, COLOR_BLACK);

    // Fill with diagonal stripes
    int fill_w = (w - 2) * percent / 100;
    if (fill_w > 0) {
        for (int py = y + 1; py < y + h - 1; py++) {
            for (int px = x + 1; px < x + 1 + fill_w; px++) {
                if ((px + py) % 2 == 0) {
                    win_buffer[py * win_w + px] = COLOR_BLACK;
                }
            }
        }
    }
}

static void draw_section_header(int y, const char *title) {
    // Draw a line with title
    buf_fill_rect(8, y, CONTENT_W - 16, 1, COLOR_BLACK);
    buf_fill_rect(8, y + 2, CONTENT_W - 16, 1, COLOR_BLACK);

    // Clear area for text
    int text_w = strlen(title) * 8 + 8;
    buf_fill_rect(12, y - 1, text_w, 5, COLOR_WHITE);

    // Draw title
    buf_draw_string(16, y - 6, title, COLOR_BLACK, COLOR_WHITE);
}

static void draw_label_value(int y, const char *label, const char *value) {
    buf_draw_string(16, y, label, COLOR_BLACK, COLOR_WHITE);
    buf_draw_string(120, y, value, COLOR_BLACK, COLOR_WHITE);
}

static void draw_all(void) {
    // Clear background
    buf_fill_rect(0, 0, win_w, win_h, COLOR_WHITE);

    char buf[64];
    int y = 8;

    // ============ Overview Section ============
    draw_section_header(y + 4, "Overview");
    y += 16;

    // Uptime
    unsigned long ticks = api->get_uptime_ticks();
    format_uptime(buf, ticks);
    draw_label_value(y, "Uptime:", buf);
    y += 16;

    // Date/Time
    int year, month, day, hour, minute, second, weekday;
    api->get_datetime(&year, &month, &day, &hour, &minute, &second, &weekday);
    format_datetime(buf, year, month, day, hour, minute, second);
    draw_label_value(y, "Time:", buf);
    y += 20;

    // ============ Memory Section ============
    draw_section_header(y + 4, "Memory");
    y += 16;

    // RAM total
    size_t ram_total = api->get_ram_total();
    format_size_mb(buf, ram_total);
    draw_label_value(y, "RAM Total:", buf);
    y += 16;

    // Heap used/free
    size_t mem_used = api->get_mem_used();
    size_t mem_free = api->get_mem_free();
    size_t mem_total = mem_used + mem_free;
    int mem_percent = (int)((mem_used * 100) / mem_total);

    format_size_mb(buf, mem_used);
    draw_label_value(y, "Heap Used:", buf);
    y += 16;

    format_size_mb(buf, mem_free);
    draw_label_value(y, "Heap Free:", buf);
    y += 16;

    // Memory progress bar
    buf_draw_string(16, y, "Heap:", COLOR_BLACK, COLOR_WHITE);
    draw_progress_bar(70, y, CONTENT_W - 86, 12, mem_percent);

    // Show percentage
    format_num(buf, mem_percent);
    int blen = strlen(buf);
    buf[blen] = '%';
    buf[blen+1] = '\0';
    buf_draw_string(CONTENT_W - 32, y, buf, COLOR_BLACK, COLOR_WHITE);
    y += 20;

    // ============ Debug Memory Section ============
    draw_section_header(y + 4, "Memory Debug");
    y += 16;

    // Heap bounds
    uint64_t heap_start = api->get_heap_start();
    uint64_t heap_end = api->get_heap_end();
    format_hex(buf, heap_start);
    draw_label_value(y, "Heap Start:", buf);
    y += 16;

    format_hex(buf, heap_end);
    draw_label_value(y, "Heap End:", buf);
    y += 16;

    // Heap size
    uint64_t heap_size = heap_end - heap_start;
    format_size_mb(buf, heap_size);
    draw_label_value(y, "Heap Size:", buf);
    y += 16;

    // Stack pointer
    uint64_t sp = api->get_stack_ptr();
    format_hex(buf, sp);
    draw_label_value(y, "Stack Ptr:", buf);
    y += 16;

    // Allocation count
    int alloc_count = api->get_alloc_count();
    format_num(buf, alloc_count);
    draw_label_value(y, "Allocs:", buf);
    y += 20;

    // ============ Disk Section ============
    draw_section_header(y + 4, "Disk");
    y += 16;

    int disk_total = api->get_disk_total();
    format_size_kb(buf, disk_total);
    draw_label_value(y, "Size:", buf);
    y += 20;

    // ============ Processes Section ============
    draw_section_header(y + 4, "Processes");
    y += 16;

    int proc_count = api->get_process_count();
    format_num(buf, proc_count);
    strcat(buf, " active");
    draw_label_value(y, "Count:", buf);
    y += 16;

    // List active processes (no limit)
    const char *state_names[] = { "-", "Ready", "Run", "Block", "Zombie" };
    int shown = 0;
    for (int i = 0; i < MAX_PROCESSES; i++) {
        char name[32];
        int state;
        if (api->get_process_info(i, name, sizeof(name), &state)) {
            // Truncate long names
            if (strlen(name) > 12) {
                name[12] = '\0';
            }
            buf_draw_string(16, y, name, COLOR_BLACK, COLOR_WHITE);

            const char *state_str = (state >= 0 && state <= 4) ? state_names[state] : "?";
            buf_draw_string(130, y, state_str, COLOR_BLACK, COLOR_WHITE);
            y += 14;
            shown++;
        }
    }
    if (shown == 0) {
        buf_draw_string(16, y, "(none)", COLOR_BLACK, COLOR_WHITE);
        y += 14;
    }
    y += 6;

    // ============ Sound Section ============
    draw_section_header(y + 4, "Sound");
    y += 16;

    const char *sound_status;
    if (api->sound_is_playing()) {
        sound_status = "Playing";
    } else if (api->sound_is_paused()) {
        sound_status = "Paused";
    } else {
        sound_status = "Idle";
    }
    draw_label_value(y, "Status:", sound_status);

    api->window_invalidate(window_id);
}

// ============ CLI Output ============

static void print_cli(void) {
    char buf[64];

    // Helper to print output
    void (*out)(const char *) = api->stdio_puts ? api->stdio_puts : api->puts;

    out("=== System Monitor ===\n\n");

    // Uptime
    unsigned long ticks = api->get_uptime_ticks();
    format_uptime(buf, ticks);
    out("Uptime:     ");
    out(buf);
    out("\n");

    // Date/Time
    int year, month, day, hour, minute, second, weekday;
    api->get_datetime(&year, &month, &day, &hour, &minute, &second, &weekday);
    format_datetime(buf, year, month, day, hour, minute, second);
    out("Time:       ");
    out(buf);
    out("\n\n");

    // RAM
    size_t ram_total = api->get_ram_total();
    format_size_mb(buf, ram_total);
    out("RAM Total:  ");
    out(buf);
    out("\n");

    // Heap
    size_t mem_used = api->get_mem_used();
    size_t mem_free = api->get_mem_free();
    size_t mem_total = mem_used + mem_free;
    int mem_percent = (int)((mem_used * 100) / mem_total);

    format_size_mb(buf, mem_used);
    out("Heap Used:  ");
    out(buf);
    out("\n");

    format_size_mb(buf, mem_free);
    out("Heap Free:  ");
    out(buf);
    out(" (");
    format_num(buf, mem_percent);
    out(buf);
    out("% used)\n\n");

    // Disk
    int disk_total = api->get_disk_total();
    format_size_kb(buf, disk_total);
    out("Disk Size:  ");
    out(buf);
    out("\n\n");

    // Processes
    int proc_count = api->get_process_count();
    out("Processes:  ");
    format_num(buf, proc_count);
    out(buf);
    out(" active\n");

    const char *state_names[] = { "-", "Ready", "Run", "Block", "Zombie" };
    for (int i = 0; i < MAX_PROCESSES; i++) {
        char name[32];
        int state;
        if (api->get_process_info(i, name, sizeof(name), &state)) {
            out("  ");
            out(name);
            // Pad to 16 chars
            int pad = 16 - strlen(name);
            while (pad-- > 0) out(" ");
            const char *state_str = (state >= 0 && state <= 4) ? state_names[state] : "?";
            out(state_str);
            out("\n");
        }
    }
    out("\n");

    // Sound
    out("Sound:      ");
    if (api->sound_is_playing()) {
        out("Playing\n");
    } else if (api->sound_is_paused()) {
        out("Paused\n");
    } else {
        out("Idle\n");
    }
}

// ============ Main ============

int main(kapi_t *kapi, int argc, char **argv) {
    (void)argc;
    (void)argv;

    api = kapi;

    // If no window API, run as CLI tool
    if (!api->window_create) {
        print_cli();
        return 0;
    }

    // Create window
    window_id = api->window_create(250, 100, CONTENT_W, CONTENT_H + 18, "System Monitor");
    if (window_id < 0) {
        api->puts("sysmon: failed to create window\n");
        return 1;
    }

    // Get buffer
    win_buffer = api->window_get_buffer(window_id, &win_w, &win_h);
    if (!win_buffer) {
        api->puts("sysmon: failed to get window buffer\n");
        api->window_destroy(window_id);
        return 1;
    }

    // Initialize graphics context
    gfx_init(&gfx, win_buffer, win_w, win_h, api->font_data);

    // Initial draw
    draw_all();

    // Event loop with periodic refresh
    int running = 1;
    int refresh_counter = 0;

    while (running) {
        int event_type, data1, data2, data3;
        while (api->window_poll_event(window_id, &event_type, &data1, &data2, &data3)) {
            switch (event_type) {
                case WIN_EVENT_CLOSE:
                    running = 0;
                    break;
                case WIN_EVENT_KEY:
                    if (data1 == 'q' || data1 == 'Q' || data1 == 27) {
                        running = 0;
                    }
                    break;
                case WIN_EVENT_RESIZE:
                    win_buffer = api->window_get_buffer(window_id, &win_w, &win_h);
                    gfx_init(&gfx, win_buffer, win_w, win_h, api->font_data);
                    draw_all();
                    break;
            }
        }

        // Refresh display every ~1 second (60 frames * 16ms)
        refresh_counter++;
        if (refresh_counter >= 60) {
            refresh_counter = 0;
            draw_all();
        }

        api->yield();
    }

    api->window_destroy(window_id);
    return 0;
}
