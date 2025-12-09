/*
 * VibeOS Kernel API
 *
 * Function pointers passed to userspace programs.
 * Programs call kernel functions directly - no syscalls needed.
 * Win3.1 style!
 */

#ifndef KAPI_H
#define KAPI_H

#include <stdint.h>
#include <stddef.h>

// Kernel API version
#define KAPI_VERSION 1

// The kernel API structure - passed to every program
typedef struct {
    uint32_t version;

    // Console I/O
    void (*putc)(char c);
    void (*puts)(const char *s);
    void (*uart_puts)(const char *s);  // Direct UART output
    int  (*getc)(void);              // Non-blocking, returns -1 if no input
    void (*set_color)(uint32_t fg, uint32_t bg);
    void (*clear)(void);             // Clear screen
    void (*set_cursor)(int row, int col);  // Set cursor position
    void (*print_int)(int n);        // Print integer
    void (*print_hex)(uint32_t n);   // Print hex

    // Keyboard
    int  (*has_key)(void);           // Check if key available

    // Memory
    void *(*malloc)(size_t size);
    void  (*free)(void *ptr);

    // Filesystem
    void *(*open)(const char *path);  // Returns vfs_node_t*
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
    uint32_t *fb_base;               // Direct framebuffer pointer
    uint32_t fb_width;               // Screen width in pixels
    uint32_t fb_height;              // Screen height in pixels
    void (*fb_put_pixel)(uint32_t x, uint32_t y, uint32_t color);
    void (*fb_fill_rect)(uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint32_t color);
    void (*fb_draw_char)(uint32_t x, uint32_t y, char c, uint32_t fg, uint32_t bg);
    void (*fb_draw_string)(uint32_t x, uint32_t y, const char *s, uint32_t fg, uint32_t bg);

    // Font access (for custom rendering)
    const uint8_t *font_data;        // 256 chars, 16 bytes each (8x16 bitmap)

    // Mouse (for GUI programs)
    void (*mouse_get_pos)(int *x, int *y);         // Get screen position
    uint8_t (*mouse_get_buttons)(void);            // Get button state
    void (*mouse_poll)(void);                      // Poll for updates

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
    uint64_t (*get_uptime_ticks)(void);  // Get timer tick count (100 ticks/sec)
    size_t (*get_mem_used)(void);        // Get used memory in bytes
    size_t (*get_mem_free)(void);        // Get free memory in bytes

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

// Global kernel API instance
extern kapi_t kapi;

// Initialize the kernel API
void kapi_init(void);

#endif
