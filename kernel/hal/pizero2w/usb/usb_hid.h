/*
 * USB HID Support
 * Keyboard interrupt handling, ISR, polling
 */

#ifndef USB_HID_H
#define USB_HID_H

#include <stdint.h>

// Ring buffer size for keyboard reports
#define KBD_RING_SIZE 16

// Debug statistics (safe to read from main loop)
typedef struct {
    volatile uint32_t irq_count;        // Total USB IRQs
    volatile uint32_t kbd_irq_count;    // Keyboard channel IRQs
    volatile uint32_t kbd_data_count;   // Successful data transfers
    volatile uint32_t kbd_nak_count;    // NAK responses (normal)
    volatile uint32_t kbd_error_count;  // Transfer errors
    volatile uint32_t kbd_restart_count; // Transfer restarts
    volatile uint32_t port_irq_count;   // Port change IRQs
    volatile uint32_t watchdog_kicks;   // Watchdog recoveries
} usb_debug_stats_t;

// Get debug statistics (for printing from main loop)
const usb_debug_stats_t* usb_hid_get_stats(void);

// Print debug statistics (call from main loop, NOT ISR)
void usb_hid_print_stats(void);

// USB IRQ handler (registered with interrupt controller)
void usb_irq_handler(void);

// Start keyboard interrupt transfers
void usb_start_keyboard_transfer(void);

// Timer tick handler (10ms) - handles watchdog and port recovery
void hal_usb_keyboard_tick(void);

// Poll for keyboard HID reports (non-blocking)
// Returns number of bytes if data available, 0 if none, -1 on error
int hal_usb_keyboard_poll(uint8_t *report, int report_len);

#endif // USB_HID_H
