/*
 * VibeOS Shell Bootstrap
 *
 * Launches /bin/init on boot. Falls back to /bin/vibesh if init is not found,
 * then to a minimal recovery shell.
 */

#include "shell.h"
#include "console.h"
#include "keyboard.h"
#include "string.h"
#include "printf.h"
#include "fb.h"
#include "vfs.h"
#include "process.h"
#include "klog.h"
#include "memory.h"
#include <stddef.h>

#ifdef TARGET_PI
#include "hal/pizero2w/usb/usb_hid.h"
#endif

// Key codes (from user/lib/vibe.h)
#define KEY_UP     0x100
#define KEY_DOWN   0x101
#define KEY_PGUP   0x107
#define KEY_PGDN   0x108

void shell_init(void) {
    // Nothing to initialize
}

void shell_run(void) {
    console_puts("\n");
    console_set_color(COLOR_AMBER, COLOR_BLACK);
    console_puts("VibeOS v0.1\n");
    console_set_color(COLOR_WHITE, COLOR_BLACK);

    // Try to launch init
    vfs_node_t *init = vfs_lookup("/bin/init");
    if (init) {
        console_puts("Starting init...\n\n");
        int result = process_exec("/bin/init");
        console_puts("\ninit exited with status ");
        printf("%d\n", result);
    } else {
        // Fall back to vibesh if init not found
        vfs_node_t *vibesh = vfs_lookup("/bin/vibesh");
        if (vibesh) {
            console_puts("Starting vibesh (init not found)...\n\n");
            int result = process_exec("/bin/vibesh");
            console_puts("\nvibesh exited with status ");
            printf("%d\n", result);
        } else {
            console_set_color(COLOR_RED, COLOR_BLACK);
            console_puts("ERROR: Neither /bin/init nor /bin/vibesh found!\n");
            console_set_color(COLOR_WHITE, COLOR_BLACK);
            console_puts("Make sure to run 'make install-user' to install userspace programs.\n");
        }
    }

    // Fallback: minimal recovery loop
    console_puts("\n[Recovery Mode - 'gui' for desktop, 'dmesg' for kernel log, 'reboot' to restart]\n");

    char cmd[64];
    int pos = 0;

    while (1) {
        console_set_color(COLOR_RED, COLOR_BLACK);
        console_puts("recovery> ");
        console_set_color(COLOR_WHITE, COLOR_BLACK);

        pos = 0;
        while (1) {
            int c = keyboard_getc();
            if (c < 0) {
                // No input - sleep until next interrupt
                asm volatile("wfi");
                continue;
            }

            if (c == '\r' || c == '\n') {
                console_putc('\n');
                cmd[pos] = '\0';
                break;
            } else if ((c == '\b' || c == 127) && pos > 0) {
                pos--;
                console_putc('\b');
                console_putc(' ');
                console_putc('\b');
            } else if (c >= 32 && c < 127 && pos < 63) {
                cmd[pos++] = (char)c;
                console_putc((char)c);
            }
        }

        if (strcmp(cmd, "gui") == 0) {
            process_exec("/bin/desktop");
        } else if (strcmp(cmd, "vibesh") == 0) {
            process_exec("/bin/vibesh");
        } else if (strcmp(cmd, "reboot") == 0) {
            console_puts("Rebooting not implemented. Please close QEMU.\n");
        } else if (strcmp(cmd, "dmesg") == 0) {
            // Interactive kernel log viewer
            size_t log_size = klog_size();
            if (log_size == 0) {
                console_puts("(kernel log empty)\n");
            } else {
                // Read entire log into buffer
                char *log_buf = malloc(log_size + 1);
                if (!log_buf) {
                    console_puts("dmesg: out of memory\n");
                } else {
                    size_t bytes_read = klog_read(log_buf, 0, log_size);
                    log_buf[bytes_read] = '\0';

                    // Build line index
                    #define DMESG_MAX_LINES 4096
                    static size_t line_off[DMESG_MAX_LINES];
                    int line_count = 0;
                    line_off[line_count++] = 0;
                    for (size_t i = 0; i < bytes_read && line_count < DMESG_MAX_LINES; i++) {
                        if (log_buf[i] == '\n' && i + 1 < bytes_read) {
                            line_off[line_count++] = i + 1;
                        }
                    }

                    int rows = console_rows();
                    int cols = console_cols();
                    int view_rows = rows - 1;
                    int top_line = (line_count > view_rows) ? line_count - view_rows : 0;

                    console_clear();
                    console_set_cursor_enabled(0);

                    int running = 1;
                    while (running) {
                        // Draw visible lines
                        for (int i = 0; i < view_rows; i++) {
                            console_set_cursor(i, 0);
                            console_clear_to_eol();
                            int idx = top_line + i;
                            if (idx < line_count) {
                                size_t start = line_off[idx];
                                size_t end = (idx + 1 < line_count) ? line_off[idx + 1] : bytes_read;
                                if (end > start && log_buf[end - 1] == '\n') end--;
                                int col = 0;
                                for (size_t j = start; j < end && col < cols; j++) {
                                    char c = log_buf[j];
                                    if (c >= 32 && c < 127) {
                                        console_putc(c);
                                        col++;
                                    }
                                }
                            }
                        }

                        // Status bar
                        console_set_cursor(rows - 1, 0);
                        console_set_color(COLOR_BLACK, COLOR_WHITE);
                        printf(" dmesg: %d-%d/%d  q:quit j/k:scroll g/G:top/end ",
                               top_line + 1,
                               (top_line + view_rows > line_count) ? line_count : top_line + view_rows,
                               line_count);
                        console_clear_to_eol();
                        console_set_color(COLOR_WHITE, COLOR_BLACK);

                        // Wait for key
                        int c;
                        while ((c = keyboard_getc()) < 0) {
                            asm volatile("wfi");
                        }

                        switch (c) {
                            case 'q': case 'Q': case 27:
                                running = 0;
                                break;
                            case 'k': case KEY_UP:
                                if (top_line > 0) top_line--;
                                break;
                            case 'j': case KEY_DOWN:
                                if (top_line < line_count - view_rows) top_line++;
                                break;
                            case 'g':
                                top_line = 0;
                                break;
                            case 'G':
                                top_line = (line_count > view_rows) ? line_count - view_rows : 0;
                                break;
                            case 'u': case KEY_PGUP:
                                top_line -= view_rows;
                                if (top_line < 0) top_line = 0;
                                break;
                            case 'd': case ' ': case KEY_PGDN:
                                top_line += view_rows;
                                if (top_line > line_count - view_rows) top_line = line_count - view_rows;
                                if (top_line < 0) top_line = 0;
                                break;
                        }
                    }

                    console_set_cursor_enabled(1);
                    console_clear();
                    free(log_buf);
                }
            }
#ifdef TARGET_PI
        } else if (strcmp(cmd, "usbstats") == 0) {
            usb_hid_print_stats();
#endif
        } else if (pos > 0) {
            console_puts("Unknown command. Try 'gui', 'vibesh', 'dmesg', or 'reboot'.\n");
        }
    }
}
