/*
 * VibeOS Kernel API Implementation
 */

#include "kapi.h"
#include "console.h"
#include "keyboard.h"
#include "memory.h"
#include "vfs.h"
#include "process.h"
#include "fb.h"
#include "mouse.h"
#include "irq.h"

// Global kernel API instance
kapi_t kapi;

// Wrapper for exit (needs to match signature)
static void kapi_exit(int status) {
    process_exit(status);
}

// Print integer (simple implementation)
static void kapi_print_int(int n) {
    if (n < 0) {
        console_putc('-');
        n = -n;
    }
    if (n == 0) {
        console_putc('0');
        return;
    }
    char buf[12];
    int i = 0;
    while (n > 0) {
        buf[i++] = '0' + (n % 10);
        n /= 10;
    }
    while (i > 0) {
        console_putc(buf[--i]);
    }
}

// Print hex
static void kapi_print_hex(uint32_t n) {
    const char *hex = "0123456789ABCDEF";
    for (int i = 7; i >= 0; i--) {
        console_putc(hex[(n >> (i * 4)) & 0xF]);
    }
}

// Wrapper for exec
static int kapi_exec(const char *path) {
    return process_exec(path);
}

// Wrapper for exec with arguments
static int kapi_exec_args(const char *path, int argc, char **argv) {
    return process_exec_args(path, argc, argv);
}

// Wrapper for spawn - create and start a new process
static int kapi_spawn(const char *path) {
    char *argv[1] = { (char *)path };
    int pid = process_create(path, 1, argv);
    if (pid > 0) {
        process_start(pid);
    }
    return pid;
}

// Wrapper for console color
static void kapi_set_color(uint32_t fg, uint32_t bg) {
    console_set_color(fg, bg);
}

// Wrapper for VFS open
static void *kapi_open(const char *path) {
    return (void *)vfs_lookup(path);
}

// Wrapper for VFS read
static int kapi_read(void *file, char *buf, size_t size, size_t offset) {
    return vfs_read((vfs_node_t *)file, buf, size, offset);
}

// Wrapper for VFS write
static int kapi_write(void *file, const char *buf, size_t size) {
    return vfs_write((vfs_node_t *)file, buf, size);
}

// Wrapper for is_dir
static int kapi_is_dir(void *node) {
    return vfs_is_dir((vfs_node_t *)node);
}

// Wrapper for create
static void *kapi_create(const char *path) {
    return (void *)vfs_create(path);
}

// Wrapper for mkdir
static void *kapi_mkdir(const char *path) {
    return (void *)vfs_mkdir(path);
}

// Wrapper for delete
static int kapi_delete(const char *path) {
    return vfs_delete(path);
}

// Wrapper for delete_dir
static int kapi_delete_dir(const char *path) {
    return vfs_delete_dir(path);
}

// Wrapper for delete_recursive
static int kapi_delete_recursive(const char *path) {
    return vfs_delete_recursive(path);
}

// Wrapper for rename
static int kapi_rename(const char *path, const char *newname) {
    return vfs_rename(path, newname);
}

// Wrapper for readdir
static int kapi_readdir(void *dir, int index, char *name, size_t name_size, uint8_t *type) {
    return vfs_readdir((vfs_node_t *)dir, index, name, name_size, type);
}

// Wrapper for set_cwd
static int kapi_set_cwd(const char *path) {
    return vfs_set_cwd(path);
}

// Wrapper for get_cwd
static int kapi_get_cwd(char *buf, size_t size) {
    return vfs_get_cwd_path(buf, size);
}

void kapi_init(void) {
    kapi.version = KAPI_VERSION;

    // Console
    extern void uart_puts(const char *s);
    kapi.putc = console_putc;
    kapi.puts = console_puts;
    kapi.uart_puts = uart_puts;
    kapi.getc = keyboard_getc;
    kapi.set_color = kapi_set_color;
    kapi.clear = console_clear;
    kapi.set_cursor = console_set_cursor;
    kapi.print_int = kapi_print_int;
    kapi.print_hex = kapi_print_hex;

    // Keyboard
    kapi.has_key = keyboard_has_key;

    // Memory
    kapi.malloc = malloc;
    kapi.free = free;

    // Filesystem
    kapi.open = kapi_open;
    kapi.read = kapi_read;
    kapi.write = kapi_write;
    kapi.is_dir = kapi_is_dir;
    kapi.create = kapi_create;
    kapi.mkdir = kapi_mkdir;
    kapi.delete = kapi_delete;
    kapi.delete_dir = kapi_delete_dir;
    kapi.delete_recursive = kapi_delete_recursive;
    kapi.rename = kapi_rename;
    kapi.readdir = kapi_readdir;
    kapi.set_cwd = kapi_set_cwd;
    kapi.get_cwd = kapi_get_cwd;

    // Process
    kapi.exit = kapi_exit;
    kapi.exec = kapi_exec;
    kapi.exec_args = kapi_exec_args;
    kapi.yield = process_yield;
    kapi.spawn = kapi_spawn;

    // Console info
    kapi.console_rows = console_rows;
    kapi.console_cols = console_cols;

    // Framebuffer
    kapi.fb_base = fb_base;
    kapi.fb_width = fb_width;
    kapi.fb_height = fb_height;
    kapi.fb_put_pixel = fb_put_pixel;
    kapi.fb_fill_rect = fb_fill_rect;
    kapi.fb_draw_char = fb_draw_char;
    kapi.fb_draw_string = fb_draw_string;

    // Font access
    extern const uint8_t font_data[256][16];
    kapi.font_data = (const uint8_t *)font_data;

    // Mouse
    kapi.mouse_get_pos = mouse_get_screen_pos;
    kapi.mouse_get_buttons = mouse_get_buttons;
    kapi.mouse_poll = mouse_poll;

    // Window management (provided by desktop, not kernel)
    kapi.window_create = 0;
    kapi.window_destroy = 0;
    kapi.window_get_buffer = 0;
    kapi.window_poll_event = 0;
    kapi.window_invalidate = 0;
    kapi.window_set_title = 0;

    // Stdio hooks (provided by terminal emulator, not kernel)
    kapi.stdio_putc = 0;
    kapi.stdio_puts = 0;
    kapi.stdio_getc = 0;
    kapi.stdio_has_key = 0;

    // System info
    kapi.get_uptime_ticks = timer_get_ticks;
    kapi.get_mem_used = memory_used;
    kapi.get_mem_free = memory_free;
}
