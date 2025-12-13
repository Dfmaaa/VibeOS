/*
 * USB HID Support
 * Keyboard interrupt handling, ISR, polling
 *
 * Features:
 * - Ring buffer for keyboard reports (no dropped keys)
 * - Debug counters instead of printf in ISR
 * - Watchdog for stuck transfers
 */

#include "usb_hid.h"
#include "dwc2_core.h"
#include "dwc2_regs.h"
#include "../../../printf.h"
#include "../../../string.h"

// ============================================================================
// Debug Statistics (safe counters, no printf in ISR)
// ============================================================================

static usb_debug_stats_t debug_stats = {0};

const usb_debug_stats_t* usb_hid_get_stats(void) {
    return &debug_stats;
}

void usb_hid_print_stats(void) {
    printf("[USB-STATS] IRQ=%u KBD=%u data=%u NAK=%u err=%u restart=%u port=%u watchdog=%u\n",
           debug_stats.irq_count,
           debug_stats.kbd_irq_count,
           debug_stats.kbd_data_count,
           debug_stats.kbd_nak_count,
           debug_stats.kbd_error_count,
           debug_stats.kbd_restart_count,
           debug_stats.port_irq_count,
           debug_stats.watchdog_kicks);
}

// ============================================================================
// Keyboard Ring Buffer (ISR writes, main loop reads)
// ============================================================================

typedef struct {
    uint8_t reports[KBD_RING_SIZE][8];
    volatile int head;  // ISR writes here (producer)
    volatile int tail;  // Consumer reads here
} kbd_ring_t;

static kbd_ring_t kbd_ring = {0};

// Push a report to the ring buffer (called from ISR)
static inline void kbd_ring_push(const uint8_t *report) {
    int next = (kbd_ring.head + 1) % KBD_RING_SIZE;
    if (next != kbd_ring.tail) {  // Not full
        memcpy(kbd_ring.reports[kbd_ring.head], report, 8);
        kbd_ring.head = next;
    }
    // If full, drop the oldest (don't overwrite)
}

// Pop a report from the ring buffer (called from main loop)
static inline int kbd_ring_pop(uint8_t *report) {
    if (kbd_ring.head == kbd_ring.tail) {
        return 0;  // Empty
    }
    memcpy(report, kbd_ring.reports[kbd_ring.tail], 8);
    kbd_ring.tail = (kbd_ring.tail + 1) % KBD_RING_SIZE;
    return 1;
}

// ============================================================================
// DMA Buffers and State
// ============================================================================

// DMA buffer for interrupt transfers (64-byte aligned for cache)
static uint8_t __attribute__((aligned(64))) intr_dma_buffer[64];

// Data toggle for interrupt endpoint
static int keyboard_data_toggle = 0;

// Transfer state
static volatile int kbd_transfer_pending = 0;
static volatile uint32_t kbd_last_transfer_tick = 0;  // For watchdog

// Port recovery state (set by IRQ, handled by timer)
static volatile int port_reset_pending = 0;
static volatile uint32_t port_reset_start_tick = 0;

// Tick counter
static uint32_t tick_counter = 0;

// ============================================================================
// Internal Transfer Functions
// ============================================================================

// Internal: configure and start a keyboard transfer on channel 1
static void usb_do_keyboard_transfer(void) {
    int ch = 1;
    int ep = usb_state.keyboard_ep;
    int addr = usb_state.keyboard_addr;

    debug_stats.kbd_restart_count++;

    // Check if channel is still enabled (shouldn't be!)
    uint32_t old_hcchar = HCCHAR(ch);
    if (old_hcchar & HCCHAR_CHENA) {
        // Channel still active - don't start another transfer
        return;
    }

    kbd_transfer_pending = 1;
    kbd_last_transfer_tick = tick_counter;

    // Configure channel for interrupt IN endpoint
    uint32_t mps = 64;  // Full speed max
    uint32_t hcchar = (mps & HCCHAR_MPS_MASK) |
                      (ep << HCCHAR_EPNUM_SHIFT) |
                      HCCHAR_EPDIR |                              // IN direction
                      (HCCHAR_EPTYPE_INTR << HCCHAR_EPTYPE_SHIFT) |
                      (addr << HCCHAR_DEVADDR_SHIFT) |
                      (1 << HCCHAR_MC_SHIFT);

    // Odd/even frame scheduling
    uint32_t fnum = HFNUM & 0xFFFF;
    if (fnum & 1) {
        hcchar |= HCCHAR_ODDFRM;
    }

    // Clear DMA buffer and invalidate cache for receive
    memset(intr_dma_buffer, 0, 8);
    // CRITICAL: Invalidate cache so DMA writes go directly to RAM
    invalidate_data_cache_range((uintptr_t)intr_dma_buffer, 8);
    dsb();

    // Configure channel interrupts
    // Only enable CHHLTD (channel halted) - we check HCINT for details
    // This minimizes interrupts: one per transfer, not one per NAK
    HCINT(ch) = 0xFFFFFFFF;
    HCINTMSK(ch) = HCINT_CHHLTD | HCINT_XACTERR | HCINT_BBLERR;
    HCDMA(ch) = arm_to_bus(intr_dma_buffer);
    HCCHAR(ch) = hcchar;

    // Transfer size: 8 bytes, 1 packet, DATA0/DATA1 toggle
    uint32_t pid = keyboard_data_toggle ? HCTSIZ_PID_DATA1 : HCTSIZ_PID_DATA0;
    HCTSIZ(ch) = 8 | (1 << HCTSIZ_PKTCNT_SHIFT) | (pid << HCTSIZ_PID_SHIFT);
    dsb();

    // Enable channel - transfer starts, interrupt fires on completion
    HCCHAR(ch) = hcchar | HCCHAR_CHENA;
    dsb();
}

// Called from ISR to restart transfer (channel already halted)
static void usb_restart_keyboard_transfer(void) {
    usb_do_keyboard_transfer();
}

// ============================================================================
// USB IRQ Handler (NO PRINTF ALLOWED!)
// ============================================================================

void usb_irq_handler(void) {
    uint32_t gintsts = GINTSTS;
    debug_stats.irq_count++;

    // Port interrupt - check what changed and react accordingly
    // WARNING: PRTENA is W1C - writing 1 DISABLES the port!
    if (gintsts & GINTSTS_PRTINT) {
        uint32_t hprt = HPRT0;
        debug_stats.port_irq_count++;

        // Check what happened
        int port_enabled = (hprt & HPRT0_PRTENA) ? 1 : 0;
        int port_connected = (hprt & HPRT0_PRTCONNSTS) ? 1 : 0;
        int enable_changed = (hprt & HPRT0_PRTENCHNG) ? 1 : 0;
        int connect_changed = (hprt & HPRT0_PRTCONNDET) ? 1 : 0;

        // Clear W1C status bits (but NOT PRTENA!)
        uint32_t hprt_write = hprt & ~HPRT0_PRTENA;
        HPRT0 = hprt_write;
        dsb();

        // React to port changes
        if (enable_changed && !port_enabled && port_connected) {
            // Port got disabled but device still connected - need to re-reset!
            // Assert reset
            hprt = HPRT0;
            hprt &= ~(HPRT0_PRTENA | HPRT0_PRTCONNDET | HPRT0_PRTENCHNG | HPRT0_PRTOVRCURRCHNG);
            hprt |= HPRT0_PRTRST;
            HPRT0 = hprt;
            dsb();

            // Set flag for timer to complete the reset (can't block 50ms in IRQ)
            port_reset_pending = 1;
            port_reset_start_tick = 0;  // Will be set by timer
            kbd_transfer_pending = 0;   // Stop keyboard polling during reset
        }

        if (connect_changed && !port_connected) {
            // Device disconnected
            usb_state.device_connected = 0;
            usb_state.keyboard_addr = 0;
            kbd_transfer_pending = 0;
        }
    }

    // Host channel interrupt
    if (gintsts & GINTSTS_HCHINT) {
        uint32_t haint = HAINT;

        for (int ch = 0; ch < 16; ch++) {
            if (haint & (1 << ch)) {
                uint32_t hcint = HCINT(ch);

                // Channel 1 = keyboard interrupt transfers
                if (ch == 1 && usb_state.keyboard_addr != 0) {
                    debug_stats.kbd_irq_count++;

                    if (hcint & HCINT_XFERCOMPL) {
                        // Transfer complete with data
                        keyboard_data_toggle = !keyboard_data_toggle;

                        // CRITICAL: Invalidate cache to read fresh DMA data
                        invalidate_data_cache_range((uintptr_t)intr_dma_buffer, 8);

                        uint32_t remaining = HCTSIZ(1) & HCTSIZ_XFERSIZE_MASK;
                        int received = 8 - remaining;
                        if (received > 0) {
                            kbd_ring_push(intr_dma_buffer);
                            debug_stats.kbd_data_count++;
                        }
                    }
                    else if ((hcint & HCINT_CHHLTD) && (hcint & HCINT_ACK)) {
                        // Got ACK with halt - data received
                        keyboard_data_toggle = !keyboard_data_toggle;

                        // CRITICAL: Invalidate cache to read fresh DMA data
                        invalidate_data_cache_range((uintptr_t)intr_dma_buffer, 8);

                        uint32_t remaining = HCTSIZ(1) & HCTSIZ_XFERSIZE_MASK;
                        int received = 8 - remaining;
                        if (received > 0) {
                            kbd_ring_push(intr_dma_buffer);
                            debug_stats.kbd_data_count++;
                        }
                    }
                    else if (hcint & HCINT_NAK) {
                        // NAK = no data available (normal for HID when no key pressed)
                        debug_stats.kbd_nak_count++;
                    }
                    else if (hcint & (HCINT_STALL | HCINT_XACTERR | HCINT_BBLERR)) {
                        // Error - increment counter (no printf!)
                        debug_stats.kbd_error_count++;
                    }
                    // Note: CHHLTD alone (without NAK/ACK/XFERCOMPL) can happen - just means halt

                    // Clear channel interrupt first
                    HCINT(ch) = 0xFFFFFFFF;

                    // Immediately restart transfer for faster polling (~1ms vs 10ms)
                    // This ensures we catch quick keypresses and releases
                    kbd_transfer_pending = 0;
                    usb_restart_keyboard_transfer();

                    continue;  // Skip the HCINT clear below
                }

                // Clear this channel's interrupts (for non-keyboard channels)
                HCINT(ch) = 0xFFFFFFFF;
            }
        }
    }

    // Clear global interrupt status
    GINTSTS = gintsts;
}

// ============================================================================
// Public API
// ============================================================================

void usb_start_keyboard_transfer(void) {
    if (kbd_transfer_pending) {
        return;
    }
    if (usb_state.keyboard_addr == 0) {
        return;
    }

    // If channel is still active, request disable (shouldn't happen normally)
    if (HCCHAR(1) & HCCHAR_CHENA) {
        HCCHAR(1) |= HCCHAR_CHDIS;
        dsb();
        return;  // Will be restarted by ISR when halt completes
    }

    printf("[USB] Starting keyboard transfers (addr=%d ep=%d)\n",
           usb_state.keyboard_addr, usb_state.keyboard_ep);
    usb_do_keyboard_transfer();
}

// Called from timer tick (every 10ms)
// Handles: port reset recovery, watchdog for stuck transfers
void hal_usb_keyboard_tick(void) {
    tick_counter++;

    // Handle port reset recovery (set by port IRQ)
    if (port_reset_pending == 1) {
        if (port_reset_start_tick == 0) {
            // First tick after reset asserted - record start time
            port_reset_start_tick = tick_counter;
            return;
        }

        // Wait 5 ticks (50ms) then de-assert reset
        if (tick_counter - port_reset_start_tick >= 5) {
            uint32_t hprt = HPRT0;
            hprt &= ~(HPRT0_PRTENA | HPRT0_PRTCONNDET | HPRT0_PRTENCHNG | HPRT0_PRTOVRCURRCHNG);
            hprt &= ~HPRT0_PRTRST;  // De-assert reset
            HPRT0 = hprt;
            dsb();

            // Wait for port to become enabled (check in future ticks)
            port_reset_pending = 2;  // Phase 2: waiting for enable
            port_reset_start_tick = tick_counter;
        }
        return;
    }

    // Phase 2: Wait for port to enable after reset
    if (port_reset_pending == 2) {
        uint32_t hprt = HPRT0;
        if (hprt & HPRT0_PRTENA) {
            printf("[USB] Port re-enabled after reset\n");
            port_reset_pending = 0;
            // Resume keyboard polling
            if (usb_state.keyboard_addr != 0) {
                usb_do_keyboard_transfer();
            }
        } else if (tick_counter - port_reset_start_tick >= 10) {
            // Timeout - port didn't enable
            printf("[USB] Port enable timeout after reset\n");
            port_reset_pending = 0;
        }
        return;
    }

    // Normal keyboard polling and watchdog
    if (!usb_state.initialized || !usb_state.device_connected) {
        return;
    }
    if (usb_state.keyboard_addr == 0) {
        return;
    }

    // WATCHDOG: If no successful transfer in 50ms (5 ticks), force restart
    if (kbd_transfer_pending &&
        (tick_counter - kbd_last_transfer_tick) >= 5) {

        debug_stats.watchdog_kicks++;

        // Force halt channel if still active
        if (HCCHAR(1) & HCCHAR_CHENA) {
            HCCHAR(1) |= HCCHAR_CHDIS;
            dsb();
            // Wait for halt
            for (int i = 0; i < 1000; i++) {
                if (HCINT(1) & HCINT_CHHLTD) break;
            }
            HCINT(1) = 0xFFFFFFFF;
        }

        kbd_transfer_pending = 0;
        usb_do_keyboard_transfer();
        return;
    }

    // If no transfer pending and channel idle, start one (fallback)
    if (!kbd_transfer_pending) {
        if (!(HCCHAR(1) & HCCHAR_CHENA)) {
            usb_do_keyboard_transfer();
        }
    }
}

// Poll keyboard for HID report (non-blocking)
// Returns number of bytes if data available, 0 if none, -1 on error
int hal_usb_keyboard_poll(uint8_t *report, int report_len) {
    if (!usb_state.initialized || !usb_state.device_connected) {
        return -1;
    }

    if (usb_state.keyboard_addr == 0) {
        return -1;
    }

    // Pop from ring buffer
    uint8_t ring_report[8];
    if (kbd_ring_pop(ring_report)) {
        int len = (report_len < 8) ? report_len : 8;
        memcpy(report, ring_report, len);
        return len;
    }

    return 0;  // No data available
}
