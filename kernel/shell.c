/*
 * VibeOS Shell Bootstrap
 *
 * Launches /bin/vibesh on boot. Falls back to a minimal recovery shell
 * if vibesh is not found.
 */

#include "shell.h"
#include "console.h"
#include "keyboard.h"
#include "string.h"
#include "printf.h"
#include "fb.h"
#include "vfs.h"
#include "process.h"
#include <stddef.h>

#ifdef TARGET_PI
#include "hal/pizero2w/usb/usb_hid.h"
#endif

void shell_init(void) {
    // Nothing to initialize
}

void shell_run(void) {
    console_puts("\n");
    console_set_color(COLOR_AMBER, COLOR_BLACK);
    console_puts("VibeOS v0.1\n");
    console_set_color(COLOR_WHITE, COLOR_BLACK);

    // Try to launch vibesh
    vfs_node_t *vibesh = vfs_lookup("/bin/vibesh");
    if (vibesh) {
        console_puts("Starting vibesh...\n\n");
        int result = process_exec("/bin/vibesh");

        // If we get here, vibesh exited
        console_puts("\nvibesh exited with status ");
        printf("%d\n", result);
    } else {
        console_set_color(COLOR_RED, COLOR_BLACK);
        console_puts("ERROR: /bin/vibesh not found!\n");
        console_set_color(COLOR_WHITE, COLOR_BLACK);
        console_puts("Make sure to run 'make install-user' to install userspace programs.\n");
    }

    // Fallback: minimal recovery loop
    console_puts("\n[Recovery Mode - type 'gui' to launch desktop, 'reboot' to restart]\n");

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
#ifdef TARGET_PI
        } else if (strcmp(cmd, "usbstats") == 0) {
            usb_hid_print_stats();
#endif
        } else if (pos > 0) {
            console_puts("Unknown command. Try 'gui', 'vibesh', 'usbstats', or 'reboot'.\n");
        }
    }
}
