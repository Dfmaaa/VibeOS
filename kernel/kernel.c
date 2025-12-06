/*
 * VibeOS Kernel
 *
 * The main kernel entry point and core functionality.
 */

#include <stdint.h>

// QEMU virt machine PL011 UART base address
#define UART0_BASE 0x09000000

// PL011 UART registers
#define UART_DR     (*(volatile uint32_t *)(UART0_BASE + 0x00))  // Data Register
#define UART_FR     (*(volatile uint32_t *)(UART0_BASE + 0x18))  // Flag Register
#define UART_FR_TXFF (1 << 5)  // Transmit FIFO Full

void uart_putc(char c) {
    // Wait until transmit FIFO is not full
    while (UART_FR & UART_FR_TXFF) {
        asm volatile("nop");
    }
    UART_DR = c;
}

void uart_puts(const char *s) {
    while (*s) {
        if (*s == '\n') {
            uart_putc('\r');
        }
        uart_putc(*s++);
    }
}

void uart_puthex(uint64_t value) {
    const char *hex = "0123456789ABCDEF";
    uart_puts("0x");
    for (int i = 60; i >= 0; i -= 4) {
        uart_putc(hex[(value >> i) & 0xF]);
    }
}

void kernel_main(void) {
    uart_puts("\n");
    uart_puts("  ╦  ╦╦╔╗ ╔═╗╔═╗╔═╗\n");
    uart_puts("  ╚╗╔╝║╠╩╗║╣ ║ ║╚═╗\n");
    uart_puts("   ╚╝ ╩╚═╝╚═╝╚═╝╚═╝\n");
    uart_puts("\n");
    uart_puts("VibeOS v0.1 - aarch64\n");
    uart_puts("=====================\n\n");
    uart_puts("[BOOT] Kernel loaded successfully!\n");
    uart_puts("[BOOT] UART initialized.\n");
    uart_puts("[BOOT] Running on QEMU virt machine.\n");
    uart_puts("\n");
    uart_puts("Welcome to VibeOS! The vibes are immaculate.\n");
    uart_puts("\n");

    // For now, just halt
    uart_puts("[KERNEL] Entering idle loop...\n");

    while (1) {
        asm volatile("wfe");
    }
}
