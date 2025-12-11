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
#include "mouse.h"
#include "irq.h"
#include "rtc.h"
#include "virtio_sound.h"
#include "virtio_net.h"
#include "net.h"
#include "ttf.h"
#include "hal/hal.h"

// UART functions now use HAL
void uart_putc(char c) {
    hal_serial_putc(c);
}

void uart_puts(const char *s) {
    while (*s) {
        if (*s == '\n') hal_serial_putc('\r');
        hal_serial_putc(*s++);
    }
}

int uart_getc(void) {
    return hal_serial_getc();
}

int uart_getc_blocking(void) {
    int c;
    while ((c = hal_serial_getc()) < 0) {
        asm volatile("nop");
    }
    return c;
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

    // Initialize interrupt controller (GIC)
    irq_init();

    // Initialize timer (10ms tick = 100 ticks/second)
    timer_init(10);

    // Initialize RTC (real time clock)
    rtc_init();

    // Initialize keyboard
    keyboard_init();

    // Register keyboard IRQ handler
    uint32_t kbd_irq = keyboard_get_irq();
    if (kbd_irq > 0) {
        irq_register_handler(kbd_irq, keyboard_irq_handler);
        irq_enable_irq(kbd_irq);
        printf("[KERNEL] Keyboard IRQ %d registered\n", kbd_irq);
    }

    // Initialize mouse (for GUI)
    mouse_init();

    // Register mouse IRQ handler
    uint32_t mouse_irq = mouse_get_irq();
    if (mouse_irq > 0) {
        irq_register_handler(mouse_irq, mouse_irq_handler);
        irq_enable_irq(mouse_irq);
        printf("[KERNEL] Mouse IRQ %d registered\n", mouse_irq);
    }

    // Initialize block device (for persistent storage)
    virtio_blk_init();

    // Initialize sound device (for audio playback)
    virtio_sound_init();

    // Initialize network device
    virtio_net_init();

    // Register network IRQ handler
    uint32_t net_irq = virtio_net_get_irq();
    if (net_irq > 0) {
        irq_register_handler(net_irq, virtio_net_irq_handler);
        irq_enable_irq(net_irq);
        printf("[KERNEL] Network IRQ %d registered\n", net_irq);
    }

    // Initialize network stack (IP, ARP, ICMP)
    net_init();

    // Initialize filesystem (will use FAT32 if disk available)
    vfs_init();

    // Initialize TrueType font system (loads font from disk)
    if (ttf_init() < 0) {
        printf("[KERNEL] TTF init failed, using bitmap font only\n");
    }

    // Initialize kernel API (for userspace programs)
    kapi_init();
    printf("[KERNEL] Kernel API initialized\n");

    // Initialize process subsystem
    process_init();

    // Load embedded binaries into VFS
    initramfs_init();

    // Enable interrupts now that everything is initialized
    printf("[KERNEL] Enabling interrupts...\n");
    irq_enable();
    printf("[KERNEL] Interrupts enabled!\n");

    printf("\n");
    printf("[KERNEL] Starting shell...\n");

    // Run the shell
    shell_run();

    // Should never reach here
    while (1) {
        asm volatile("wfi");
    }
}
