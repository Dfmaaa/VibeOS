/*
 * VibeOS Kernel
 *
 * The main kernel entry point and core functionality.
 */

#include <stdint.h>
#include "memory.h"
#include "string.h"
#include "printf.h"
#include "fb.h"
#include "console.h"
#include "keyboard.h"
#include "shell.h"
#include "vfs.h"
#include "process.h"
#include "initramfs.h"
#include "kapi.h"
#include "virtio_blk.h"

// QEMU virt machine PL011 UART base address
#define UART0_BASE 0x09000000

// PL011 UART registers
#define UART_DR     (*(volatile uint32_t *)(UART0_BASE + 0x00))  // Data Register
#define UART_FR     (*(volatile uint32_t *)(UART0_BASE + 0x18))  // Flag Register
#define UART_FR_TXFF (1 << 5)  // Transmit FIFO Full
#define UART_FR_RXFE (1 << 4)  // Receive FIFO Empty

void uart_putc(char c) {
    // Wait until transmit FIFO is not full
    while (UART_FR & UART_FR_TXFF) {
        asm volatile("nop");
    }
    UART_DR = c;
}

int uart_getc(void) {
    // Return -1 if no data available
    if (UART_FR & UART_FR_RXFE) {
        return -1;
    }
    return UART_DR & 0xFF;
}

int uart_getc_blocking(void) {
    // Wait for data
    while (UART_FR & UART_FR_RXFE) {
        asm volatile("nop");
    }
    return UART_DR & 0xFF;
}

void kernel_main(void) {
    // Raw UART test first
    uart_putc('V');
    uart_putc('I');
    uart_putc('B');
    uart_putc('E');
    uart_putc('\r');
    uart_putc('\n');

    // Test printf with simplest possible case
    uart_putc('1');
    printf("test");
    uart_putc('2');
    printf("\n");
    printf("  ╦  ╦╦╔╗ ╔═╗╔═╗╔═╗\n");
    printf("  ╚╗╔╝║╠╩╗║╣ ║ ║╚═╗\n");
    printf("   ╚╝ ╩╚═╝╚═╝╚═╝╚═╝\n");
    printf("\n");
    printf("VibeOS v0.1 - aarch64\n");
    printf("=====================\n\n");
    printf("[BOOT] Kernel loaded successfully!\n");
    printf("[BOOT] UART initialized.\n");

    // Initialize memory management
    memory_init();
    printf("[BOOT] Memory initialized.\n");
    printf("       Heap: %p - %p\n", (void *)heap_start, (void *)heap_end);
    printf("       Free: %lu MB\n", memory_free() / 1024 / 1024);

    // Test malloc
    printf("[TEST] Testing malloc...\n");
    char *test1 = malloc(100);
    char *test2 = malloc(200);
    printf("       Allocated 100 bytes at: %p\n", test1);
    printf("       Allocated 200 bytes at: %p\n", test2);

    // Write something to prove it works
    strcpy(test1, "Hi from printf!");
    printf("       Wrote to memory: %s\n", test1);

    // Free and check
    free(test1);
    free(test2);
    printf("       Freed allocations. Free: %lu MB\n", memory_free() / 1024 / 1024);

    printf("[BOOT] Running on QEMU virt machine.\n");

    // Initialize framebuffer
    if (fb_init() == 0) {
        // Initialize console
        console_init();
        printf("[FB] Console initialized: %dx%d chars\n", console_cols(), console_rows());

        // Print to console (on screen!)
        console_set_color(COLOR_GREEN, COLOR_BLACK);
        console_puts("  _   _ _ _          ___  ____  \n");
        console_puts(" | | | (_) |__   ___/ _ \\/ ___| \n");
        console_puts(" | | | | | '_ \\ / _ \\ | | \\___ \\ \n");
        console_puts(" | \\_/ | | |_) |  __/ |_| |___) |\n");
        console_puts("  \\___/|_|_.__/ \\___|\\___/|____/ \n");
        console_set_color(COLOR_WHITE, COLOR_BLACK);
        console_puts("                            by ");
        console_set_color(COLOR_AMBER, COLOR_BLACK);
        console_puts("Claude\n");
        console_puts("\n");
        console_set_color(COLOR_WHITE, COLOR_BLACK);
        console_puts("==========================================\n\n");

        console_set_color(COLOR_GREEN, COLOR_BLACK);
        console_puts("The vibes are immaculate.\n\n");

        console_set_color(COLOR_WHITE, COLOR_BLACK);
        console_puts("System ready.\n");
        console_puts("\n");
    }

    // Initialize keyboard (polling mode)
    keyboard_init();

    // Initialize block device (for persistent storage)
    virtio_blk_init();

    // Initialize filesystem (will use FAT32 if disk available)
    vfs_init();

    // Initialize kernel API (for userspace programs)
    kapi_init();
    printf("[KERNEL] Kernel API initialized\n");

    // Initialize process subsystem
    process_init();

    // Load embedded binaries into VFS
    initramfs_init();

    printf("\n");
    printf("[KERNEL] Starting shell...\n");

    // Run the shell
    shell_run();

    // Should never reach here
    while (1) {
        asm volatile("wfi");
    }
}
