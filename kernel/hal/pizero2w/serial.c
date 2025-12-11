/*
 * Raspberry Pi Zero 2W Serial Driver
 *
 * Mini UART at 0x3F215000
 *
 * The Pi has two UARTs:
 * 1. PL011 (full UART) - used by Bluetooth by default
 * 2. Mini UART - simpler, on GPIO 14/15
 *
 * We use the Mini UART since it's available on GPIO header.
 *
 * NOTE: Mini UART clock is derived from core clock, which varies!
 * For reliable serial, use config.txt: core_freq=250 or enable_uart=1
 *
 * Even though we're going framebuffer-first, having serial output
 * helps if we ever need to debug with a USB-serial adapter.
 */

#include "../hal.h"

// Peripheral base for Pi Zero 2W (BCM2710)
#define PERI_BASE       0x3F000000

// GPIO registers
#define GPIO_BASE       (PERI_BASE + 0x200000)
#define GPFSEL1         (*(volatile uint32_t *)(GPIO_BASE + 0x04))
#define GPPUD           (*(volatile uint32_t *)(GPIO_BASE + 0x94))
#define GPPUDCLK0       (*(volatile uint32_t *)(GPIO_BASE + 0x98))

// Aux / Mini UART registers
#define AUX_BASE        (PERI_BASE + 0x215000)
#define AUX_ENABLES     (*(volatile uint32_t *)(AUX_BASE + 0x04))
#define AUX_MU_IO       (*(volatile uint32_t *)(AUX_BASE + 0x40))
#define AUX_MU_IER      (*(volatile uint32_t *)(AUX_BASE + 0x44))
#define AUX_MU_IIR      (*(volatile uint32_t *)(AUX_BASE + 0x48))
#define AUX_MU_LCR      (*(volatile uint32_t *)(AUX_BASE + 0x4C))
#define AUX_MU_MCR      (*(volatile uint32_t *)(AUX_BASE + 0x50))
#define AUX_MU_LSR      (*(volatile uint32_t *)(AUX_BASE + 0x54))
#define AUX_MU_MSR      (*(volatile uint32_t *)(AUX_BASE + 0x58))
#define AUX_MU_SCRATCH  (*(volatile uint32_t *)(AUX_BASE + 0x5C))
#define AUX_MU_CNTL     (*(volatile uint32_t *)(AUX_BASE + 0x60))
#define AUX_MU_STAT     (*(volatile uint32_t *)(AUX_BASE + 0x64))
#define AUX_MU_BAUD     (*(volatile uint32_t *)(AUX_BASE + 0x68))

// LSR bits
#define AUX_MU_LSR_TX_EMPTY  (1 << 5)
#define AUX_MU_LSR_RX_READY  (1 << 0)

// Delay loop
static void delay(int count) {
    while (count--) {
        asm volatile("nop");
    }
}

void hal_serial_init(void) {
    // Enable Mini UART
    AUX_ENABLES = 1;

    // Disable TX/RX while configuring
    AUX_MU_CNTL = 0;

    // Disable interrupts
    AUX_MU_IER = 0;

    // 8-bit mode
    AUX_MU_LCR = 3;

    // RTS high (we don't use flow control)
    AUX_MU_MCR = 0;

    // Set baud rate
    // Baud = system_clock / (8 * (AUX_MU_BAUD + 1))
    // For 250MHz core clock and 115200 baud: 250000000 / (8 * 115200) - 1 = 270
    AUX_MU_BAUD = 270;

    // Clear FIFOs
    AUX_MU_IIR = 0xC6;

    // Set GPIO 14 and 15 to ALT5 (Mini UART)
    // GPFSEL1 controls GPIO 10-19
    // GPIO 14 = bits 12-14 = ALT5 (010)
    // GPIO 15 = bits 15-17 = ALT5 (010)
    uint32_t sel = GPFSEL1;
    sel &= ~(7 << 12);  // Clear GPIO 14
    sel &= ~(7 << 15);  // Clear GPIO 15
    sel |= (2 << 12);   // ALT5 for GPIO 14
    sel |= (2 << 15);   // ALT5 for GPIO 15
    GPFSEL1 = sel;

    // Disable pull-up/pull-down on GPIO 14, 15
    GPPUD = 0;
    delay(150);
    GPPUDCLK0 = (1 << 14) | (1 << 15);
    delay(150);
    GPPUDCLK0 = 0;

    // Enable TX and RX
    AUX_MU_CNTL = 3;
}

void hal_serial_putc(char c) {
    // Wait for TX to be empty
    while (!(AUX_MU_LSR & AUX_MU_LSR_TX_EMPTY)) {
        asm volatile("nop");
    }
    AUX_MU_IO = c;
}

int hal_serial_getc(void) {
    // Check if data available
    if (!(AUX_MU_LSR & AUX_MU_LSR_RX_READY)) {
        return -1;
    }
    return AUX_MU_IO & 0xFF;
}
