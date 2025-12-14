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
#include "klog.h"
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

    // Initialize kernel log first (static buffer, no malloc needed)
    klog_init();

    // Initialize memory management first (needed for malloc)
    memory_init();

    // Initialize framebuffer and console ASAP so printf goes to screen on Pi
    fb_init();
    console_init();


    // Now printf works on both UART (QEMU) and screen (Pi)
    printf("  ╦  ╦╦╔╗ ╔═╗╔═╗╔═╗\n");
    printf("  ╚╗╔╝║╠╩╗║╣ ║ ║╚═╗\n");
    printf("   ╚╝ ╩╚═╝╚═╝╚═╝╚═╝\n");
    printf("\n");
    printf("VibeOS v0.1 - aarch64\n");
    printf("=====================\n\n");
    printf("[BOOT] Kernel loaded successfully!\n");
    printf("[BOOT] UART initialized.\n");
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

    // Splash screen
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

#ifdef TARGET_QEMU
    // Initialize interrupt controller (GIC)
    irq_init();

    // Initialize timer (10ms tick = 100 ticks/second, preemption every 5 ticks)
    timer_init(10);

    // Initialize RTC (real time clock)
    rtc_init();
#else
    // Pi: Initialize BCM2836 ARM Local + BCM2835 interrupt controllers
    hal_irq_init();

    // Initialize timer (10ms tick, preemption every 5 ticks = 50ms)
    hal_timer_init(10);

    // Initialize GPIO LED
    hal_led_init();

    // Enable interrupts early so USB can use timer-based delays
    printf("[KERNEL] Enabling interrupts for USB init...\n");
    hal_irq_enable();
#endif

#ifdef TARGET_QEMU
    // Initialize keyboard (virtio-input on QEMU)
    keyboard_init();

    // Register keyboard IRQ handler
    uint32_t kbd_irq = keyboard_get_irq();
    if (kbd_irq > 0) {
        irq_register_handler(kbd_irq, keyboard_irq_handler);
        irq_enable_irq(kbd_irq);
        printf("[KERNEL] Keyboard IRQ %d registered\n", kbd_irq);
    }

    // Initialize mouse (virtio-tablet on QEMU)
    mouse_init();

    // Register mouse IRQ handler
    uint32_t mouse_irq = mouse_get_irq();
    if (mouse_irq > 0) {
        irq_register_handler(mouse_irq, mouse_irq_handler);
        irq_enable_irq(mouse_irq);
        printf("[KERNEL] Mouse IRQ %d registered\n", mouse_irq);
    }
#else
    // Pi: Initialize USB controller for keyboard/mouse
    if (hal_usb_init() < 0) {
        printf("[KERNEL] USB init failed - no USB input devices\n");
    }
#endif

#ifdef PI_DEBUG_MODE
    // Debug mode: skip all non-essential init and run USB debug loop
    printf("\n");
    printf("[DEBUG] ==========================================\n");
    printf("[DEBUG] Pi USB Debug Mode - Minimal Boot\n");
    printf("[DEBUG] Skipping: SD, VFS, TTF, shell\n");
    printf("[DEBUG] ==========================================\n");
    printf("\n");

    // Interrupts already enabled before USB init
    usb_keyboard_debug_loop();  // Never returns
#endif

    // Initialize block device (for persistent storage)
#ifdef TARGET_QEMU
    virtio_blk_init();
#else
    // For Pi, use HAL block device (EMMC/SD card)
    if (hal_blk_init() < 0) {
        printf("[KERNEL] Block device init failed!\n");
    }
#endif

#ifdef TARGET_QEMU
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
#endif

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

#ifdef TARGET_QEMU
    // Enable interrupts now that everything is initialized
    printf("[KERNEL] Enabling interrupts...\n");
    irq_enable();
    printf("[KERNEL] Interrupts enabled!\n");
#endif
    // Pi: interrupts already enabled before USB init

    printf("\n");
    printf("[KERNEL] Starting shell...\n");

    // Run the shell
    shell_run();

    // Should never reach here
    while (1) {
        asm volatile("wfi");
    }
}
