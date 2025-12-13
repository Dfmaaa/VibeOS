/*
 * USB Transfer Functions
 * Control transfers and DMA handling
 */

#include "usb_transfer.h"
#include "dwc2_core.h"
#include "dwc2_regs.h"
#include "../../../printf.h"
#include "../../../string.h"

// Wait for DMA transfer to complete
int usb_wait_for_dma_complete(int ch, int max_retries) {
    for (int retry = 0; retry < max_retries; retry++) {
        int timeout = 100000;
        while (timeout--) {
            uint32_t hcint = HCINT(ch);

            if (hcint & HCINT_XFERCOMPL) {
                HCINT(ch) = 0xFFFFFFFF;
                return 0;  // Success
            }
            if (hcint & HCINT_CHHLTD) {
                // Channel halted - check why
                if (hcint & (HCINT_XFERCOMPL | HCINT_ACK)) {
                    HCINT(ch) = 0xFFFFFFFF;
                    return 0;  // Transfer actually completed
                }
                if (hcint & HCINT_NAK) {
                    // NAK - need to retry
                    HCINT(ch) = 0xFFFFFFFF;
                    break;  // Break to retry loop
                }
                if (hcint & (HCINT_STALL | HCINT_XACTERR | HCINT_BBLERR | HCINT_AHBERR)) {
                    usb_debug("[USB] Transfer error: hcint=%08x\n", hcint);
                    HCINT(ch) = 0xFFFFFFFF;
                    return -1;
                }
                // Other halt reason - assume done
                HCINT(ch) = 0xFFFFFFFF;
                return 0;
            }
            if (hcint & HCINT_AHBERR) {
                usb_debug("[USB] AHB error (bad DMA address?)\n");
                HCINT(ch) = 0xFFFFFFFF;
                return -1;
            }
            if (hcint & HCINT_STALL) {
                usb_debug("[USB] STALL\n");
                HCINT(ch) = 0xFFFFFFFF;
                return -1;
            }
            if (hcint & HCINT_BBLERR) {
                usb_debug("[USB] Babble error\n");
                HCINT(ch) = 0xFFFFFFFF;
                return -1;
            }
            if (hcint & HCINT_XACTERR) {
                usb_debug("[USB] Transaction error\n");
                HCINT(ch) = 0xFFFFFFFF;
                return -1;
            }

            usleep(1);
        }

        if (retry < max_retries - 1) {
            usb_debug("[USB] Retry %d/%d\n", retry + 1, max_retries);
            // Re-enable channel for retry
            uint32_t hcchar = HCCHAR(ch);
            hcchar |= HCCHAR_CHENA;
            hcchar &= ~HCCHAR_CHDIS;
            HCCHAR(ch) = hcchar;
            dsb();
            usleep(1000);
        }
    }

    usb_debug("[USB] Transfer timeout after %d retries\n", max_retries);
    return -1;
}

// Control transfer using DMA (SETUP + optional DATA + STATUS)
int usb_control_transfer(int device_addr, usb_setup_packet_t *setup,
                         void *data, int data_len, int data_in) {
    int ch = 0;  // Use channel 0 for control

    usb_debug("[USB] Control: addr=%d req=%02x val=%04x len=%d %s\n",
              device_addr, setup->bRequest, setup->wValue, data_len,
              data_in ? "IN" : "OUT");

    // Halt channel if active
    usb_halt_channel(ch);

    // Configure channel for control endpoint
    // Look up device to get MPS and speed
    uint32_t mps = 64;  // Default for FS/HS
    int dev_speed = usb_state.device_speed;

    if (device_addr == 0) {
        mps = (usb_state.device_speed == 2) ? 8 : 64;  // LS=8, FS/HS=64
    } else {
        // Find device in our list
        for (int i = 0; i < usb_state.num_devices; i++) {
            if (usb_state.devices[i].address == device_addr) {
                mps = usb_state.devices[i].max_packet_size;
                dev_speed = usb_state.devices[i].speed;
                break;
            }
        }
        if (mps == 0) mps = 64;
    }

    uint32_t hcchar_base = (mps & HCCHAR_MPS_MASK) |
                           (0 << HCCHAR_EPNUM_SHIFT) |         // EP0
                           (HCCHAR_EPTYPE_CTRL << HCCHAR_EPTYPE_SHIFT) |
                           (device_addr << HCCHAR_DEVADDR_SHIFT) |
                           (1 << HCCHAR_MC_SHIFT);             // 1 transaction per frame

    if (dev_speed == 2) {  // Low-speed
        hcchar_base |= HCCHAR_LSDEV;
    }

    // ========== SETUP Stage (DMA) ==========
    usb_debug("[USB] SETUP stage (DMA)...\n");

    // Copy SETUP packet to DMA buffer
    memcpy(dma_buffer, setup, 8);
    // CRITICAL: Flush CPU cache so DMA controller sees the data!
    clean_data_cache_range((uintptr_t)dma_buffer, 8);
    dsb();

    // Clear all channel interrupts
    HCINT(ch) = 0xFFFFFFFF;

    // Enable interrupts for this channel
    HCINTMSK(ch) = HCINT_XFERCOMPL | HCINT_CHHLTD | HCINT_STALL |
                   HCINT_NAK | HCINT_ACK | HCINT_XACTERR | HCINT_BBLERR | HCINT_AHBERR;

    // Set DMA address (bus address)
    HCDMA(ch) = arm_to_bus(dma_buffer);
    dsb();

    // Configure channel (OUT direction for SETUP)
    HCCHAR(ch) = hcchar_base;
    dsb();

    // Transfer size: 8 bytes, 1 packet, SETUP PID
    HCTSIZ(ch) = 8 | (1 << HCTSIZ_PKTCNT_SHIFT) | (HCTSIZ_PID_SETUP << HCTSIZ_PID_SHIFT);
    dsb();

    usb_debug("[USB] SETUP: HCDMA=%08x HCCHAR=%08x HCTSIZ=%08x\n",
              HCDMA(ch), HCCHAR(ch), HCTSIZ(ch));

    // Enable channel to start the transfer
    HCCHAR(ch) = hcchar_base | HCCHAR_CHENA;
    dsb();

    // Wait for SETUP completion
    if (usb_wait_for_dma_complete(ch, 5) < 0) {
        usb_debug("[USB] SETUP failed\n");
        return -1;
    }
    usb_debug("[USB] SETUP complete\n");

    // ========== DATA Stage (if any) ==========
    int bytes_transferred = 0;

    if (data_len > 0 && data != NULL) {
        usb_debug("[USB] DATA stage (%d bytes, %s)...\n", data_len, data_in ? "IN" : "OUT");

        if (data_len > 512) {
            usb_debug("[USB] Data too large for DMA buffer\n");
            return -1;
        }

        // Configure for data direction
        uint32_t data_hcchar = hcchar_base;
        if (data_in) {
            data_hcchar |= HCCHAR_EPDIR;  // IN
            // Clear DMA buffer for IN transfer
            memset(dma_buffer, 0, data_len);
            // Invalidate cache - ensures we don't hold stale lines that could
            // be evicted into the buffer while DMA is writing
            invalidate_data_cache_range((uintptr_t)dma_buffer, data_len);
        } else {
            // Copy data to DMA buffer for OUT transfer
            memcpy(dma_buffer, data, data_len);
            // Flush cache so DMA controller sees the data
            clean_data_cache_range((uintptr_t)dma_buffer, data_len);
        }
        dsb();

        // Calculate packet count
        int pkt_count = (data_len + mps - 1) / mps;
        if (pkt_count == 0) pkt_count = 1;

        // Clear interrupts
        HCINT(ch) = 0xFFFFFFFF;

        // Set DMA address
        HCDMA(ch) = arm_to_bus(dma_buffer);
        dsb();

        // Configure channel
        HCCHAR(ch) = data_hcchar;
        dsb();

        // Transfer size, packet count, DATA1 PID (first data after SETUP is always DATA1)
        HCTSIZ(ch) = data_len | (pkt_count << HCTSIZ_PKTCNT_SHIFT) |
                     (HCTSIZ_PID_DATA1 << HCTSIZ_PID_SHIFT);
        dsb();

        usb_debug("[USB] DATA: HCDMA=%08x HCCHAR=%08x HCTSIZ=%08x\n",
                  HCDMA(ch), HCCHAR(ch), HCTSIZ(ch));

        // Enable channel
        HCCHAR(ch) = data_hcchar | HCCHAR_CHENA;
        dsb();

        // Wait for completion
        if (usb_wait_for_dma_complete(ch, 10) < 0) {
            usb_debug("[USB] DATA stage failed\n");
            return -1;
        }

        if (data_in) {
            // Invalidate cache to ensure CPU reads fresh data from RAM
            invalidate_data_cache_range((uintptr_t)dma_buffer, data_len);

            // Copy received data from DMA buffer
            // Calculate actual bytes received from HCTSIZ
            uint32_t remaining = HCTSIZ(ch) & HCTSIZ_XFERSIZE_MASK;
            bytes_transferred = data_len - remaining;
            if (bytes_transferred > 0) {
                memcpy(data, dma_buffer, bytes_transferred);
            }
            usb_debug("[USB] DATA IN: received %d bytes\n", bytes_transferred);
        } else {
            bytes_transferred = data_len;
            usb_debug("[USB] DATA OUT: sent %d bytes\n", bytes_transferred);
        }
    }

    // ========== STATUS Stage ==========
    usb_debug("[USB] STATUS stage...\n");

    // Status is opposite direction of data (or IN if no data)
    int status_in = (data_len > 0) ? !data_in : 1;

    uint32_t status_hcchar = hcchar_base;
    if (status_in) {
        status_hcchar |= HCCHAR_EPDIR;
    }

    // Clear interrupts
    HCINT(ch) = 0xFFFFFFFF;

    // Set DMA address (zero-length, but still need valid address)
    HCDMA(ch) = arm_to_bus(dma_buffer);
    dsb();

    // Configure channel
    HCCHAR(ch) = status_hcchar;
    dsb();

    // Zero-length packet, DATA1 PID
    HCTSIZ(ch) = 0 | (1 << HCTSIZ_PKTCNT_SHIFT) | (HCTSIZ_PID_DATA1 << HCTSIZ_PID_SHIFT);
    dsb();

    usb_debug("[USB] STATUS: HCDMA=%08x HCCHAR=%08x HCTSIZ=%08x\n",
              HCDMA(ch), HCCHAR(ch), HCTSIZ(ch));

    // Enable channel
    HCCHAR(ch) = status_hcchar | HCCHAR_CHENA;
    dsb();

    // Wait for completion
    if (usb_wait_for_dma_complete(ch, 5) < 0) {
        usb_debug("[USB] STATUS failed\n");
        return -1;
    }

    usb_debug("[USB] Control transfer complete, %d bytes\n", bytes_transferred);
    return bytes_transferred;
}
