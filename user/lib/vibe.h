/*
 * VibeOS Userspace Library
 *
 * Programs receive a pointer to kernel API and call functions directly.
 * No syscalls needed - Win3.1 style!
 */

#ifndef _VIBE_H
#define _VIBE_H

typedef unsigned long size_t;
typedef unsigned char uint8_t;
typedef unsigned short uint16_t;
typedef unsigned int uint32_t;
typedef unsigned long uint64_t;
typedef signed short int16_t;

// Kernel API structure (must match kernel/kapi.h)
typedef struct kapi {
    uint32_t version;

    // Console I/O
    void (*putc)(char c);
    void (*puts)(const char *s);
    void (*uart_puts)(const char *s);  // Direct UART output
    int  (*getc)(void);
    void (*set_color)(uint32_t fg, uint32_t bg);
    void (*clear)(void);
    void (*set_cursor)(int row, int col);
    void (*print_int)(int n);
    void (*print_hex)(uint32_t n);

    // Keyboard
    int  (*has_key)(void);

    // Memory
    void *(*malloc)(size_t size);
    void  (*free)(void *ptr);

    // Filesystem
    void *(*open)(const char *path);
    int   (*read)(void *file, char *buf, size_t size, size_t offset);
    int   (*write)(void *file, const char *buf, size_t size);
    int   (*is_dir)(void *node);
    int   (*file_size)(void *node);   // Get file size in bytes
    void *(*create)(const char *path);
    void *(*mkdir)(const char *path);
    int   (*delete)(const char *path);
    int   (*delete_dir)(const char *path);  // Delete empty directory
    int   (*delete_recursive)(const char *path);  // Delete file or dir recursively
    int   (*rename)(const char *path, const char *newname);
    int   (*readdir)(void *dir, int index, char *name, size_t name_size, uint8_t *type);
    int   (*set_cwd)(const char *path);
    int   (*get_cwd)(char *buf, size_t size);

    // Process
    void (*exit)(int status);
    int  (*exec)(const char *path);   // Run another program (waits for completion)
    int  (*exec_args)(const char *path, int argc, char **argv);  // Run with arguments
    void (*yield)(void);              // Give up CPU to other processes
    int  (*spawn)(const char *path);  // Start a new process (returns immediately)

    // Console info
    int  (*console_rows)(void);       // Get number of console rows
    int  (*console_cols)(void);       // Get number of console columns

    // Framebuffer (for GUI programs)
    uint32_t *fb_base;
    uint32_t fb_width;
    uint32_t fb_height;
    void (*fb_put_pixel)(uint32_t x, uint32_t y, uint32_t color);
    void (*fb_fill_rect)(uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint32_t color);
    void (*fb_draw_char)(uint32_t x, uint32_t y, char c, uint32_t fg, uint32_t bg);
    void (*fb_draw_string)(uint32_t x, uint32_t y, const char *s, uint32_t fg, uint32_t bg);

    // Font access (for custom rendering)
    const uint8_t *font_data;        // 256 chars, 16 bytes each (8x16 bitmap)

    // Mouse (for GUI programs)
    void (*mouse_get_pos)(int *x, int *y);
    uint8_t (*mouse_get_buttons)(void);
    void (*mouse_poll)(void);

    // Window management (for desktop apps)
    // These are set by the desktop window server, not the kernel
    int  (*window_create)(int x, int y, int w, int h, const char *title);
    void (*window_destroy)(int wid);
    uint32_t *(*window_get_buffer)(int wid, int *w, int *h);
    int  (*window_poll_event)(int wid, int *event_type, int *data1, int *data2, int *data3);
    void (*window_invalidate)(int wid);
    void (*window_set_title)(int wid, const char *title);

    // Stdio hooks (for terminal emulator)
    // If set, shell uses these instead of console I/O
    void (*stdio_putc)(char c);          // Write a character
    void (*stdio_puts)(const char *s);   // Write a string
    int  (*stdio_getc)(void);            // Read a character (-1 if none)
    int  (*stdio_has_key)(void);         // Check if input available

    // System info
    unsigned long (*get_uptime_ticks)(void);  // Get timer tick count (100 ticks/sec)
    size_t (*get_mem_used)(void);             // Get used memory in bytes
    size_t (*get_mem_free)(void);             // Get free memory in bytes

    // RTC (Real Time Clock)
    uint32_t (*get_timestamp)(void);     // Unix timestamp (seconds since 1970)
    void (*get_datetime)(int *year, int *month, int *day,
                         int *hour, int *minute, int *second, int *weekday);

    // Power management / timing
    void (*wfi)(void);                   // Wait for interrupt (low power sleep)
    void (*sleep_ms)(uint32_t ms);       // Sleep for at least ms milliseconds

    // Sound
    int (*sound_play_wav)(const void *data, uint32_t size);  // Play WAV from memory (legacy)
    void (*sound_stop)(void);                                 // Stop playback
    int (*sound_is_playing)(void);                           // Check if playing
    int (*sound_play_pcm)(const void *data, uint32_t samples, uint8_t channels, uint32_t sample_rate);  // Play raw S16LE PCM (blocking)
    int (*sound_play_pcm_async)(const void *data, uint32_t samples, uint8_t channels, uint32_t sample_rate);  // Play raw S16LE PCM (non-blocking)
    void (*sound_pause)(void);                               // Pause playback (can resume)
    int (*sound_resume)(void);                               // Resume paused playback
    int (*sound_is_paused)(void);                            // Check if paused
} kapi_t;

// Window event types
#define WIN_EVENT_NONE       0
#define WIN_EVENT_MOUSE_DOWN 1
#define WIN_EVENT_MOUSE_UP   2
#define WIN_EVENT_MOUSE_MOVE 3
#define WIN_EVENT_KEY        4
#define WIN_EVENT_CLOSE      5
#define WIN_EVENT_FOCUS      6
#define WIN_EVENT_UNFOCUS    7
#define WIN_EVENT_RESIZE     8

// Mouse button masks
#define MOUSE_BTN_LEFT   0x01
#define MOUSE_BTN_RIGHT  0x02
#define MOUSE_BTN_MIDDLE 0x04

// Special key codes (must match kernel/keyboard.c)
#define KEY_UP     0x100
#define KEY_DOWN   0x101
#define KEY_LEFT   0x102
#define KEY_RIGHT  0x103
#define KEY_HOME   0x104
#define KEY_END    0x105
#define KEY_DELETE 0x106

// Colors (must match kernel fb.h - these are RGB values)
#define COLOR_BLACK   0x00000000
#define COLOR_WHITE   0x00FFFFFF
#define COLOR_RED     0x00FF0000
#define COLOR_GREEN   0x0000FF00
#define COLOR_BLUE    0x000000FF
#define COLOR_CYAN    0x0000FFFF
#define COLOR_MAGENTA 0x00FF00FF
#define COLOR_YELLOW  0x00FFFF00
#define COLOR_AMBER   0x00FFBF00

// NULL pointer
#ifndef NULL
#define NULL ((void *)0)
#endif

// ============ String Functions ============

static inline size_t strlen(const char *s) {
    size_t len = 0;
    while (s[len]) len++;
    return len;
}

static inline int strcmp(const char *a, const char *b) {
    while (*a && *b && *a == *b) {
        a++;
        b++;
    }
    return *a - *b;
}

static inline int strncmp(const char *a, const char *b, size_t n) {
    while (n > 0 && *a && *b && *a == *b) {
        a++;
        b++;
        n--;
    }
    if (n == 0) return 0;
    return *a - *b;
}

static inline char *strcpy(char *dst, const char *src) {
    char *d = dst;
    while ((*d++ = *src++));
    return dst;
}

static inline char *strncpy_safe(char *dst, const char *src, size_t n) {
    size_t i;
    for (i = 0; i < n - 1 && src[i]; i++) {
        dst[i] = src[i];
    }
    dst[i] = '\0';
    return dst;
}

static inline char *strcat(char *dst, const char *src) {
    char *d = dst;
    while (*d) d++;
    while ((*d++ = *src++));
    return dst;
}

static inline void *memset(void *s, int c, size_t n) {
    unsigned char *p = (unsigned char *)s;
    while (n--) *p++ = (unsigned char)c;
    return s;
}

static inline void *memcpy(void *dst, const void *src, size_t n) {
    unsigned char *d = (unsigned char *)dst;
    const unsigned char *s = (const unsigned char *)src;
    while (n--) *d++ = *s++;
    return dst;
}

// Check if character is whitespace
static inline int isspace(int c) {
    return c == ' ' || c == '\t' || c == '\n' || c == '\r';
}

// Check if character is printable
static inline int isprint(int c) {
    return c >= 32 && c < 127;
}

#endif
