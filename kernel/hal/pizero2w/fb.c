/*
 * Raspberry Pi Zero 2W Framebuffer Driver
 *
 * Uses VideoCore mailbox to request framebuffer from GPU
 *
 * Pi Zero 2W (BCM2710) memory map:
 * - ARM physical addresses: 0x00000000 - 0x3FFFFFFF
 * - Peripherals: 0x3F000000 (mapped from bus 0x7E000000)
 * - Kernel loads at: 0x80000
 *
 * Mailbox is at 0x3F00B880 (ARM physical)
 */

#include "../hal.h"

// Forward declarations for printf (may not be available early)
extern void uart_putc(char c);

// Framebuffer info
static hal_fb_info_t fb_info = {0};

// Mailbox registers (ARM physical addresses for Pi Zero 2W / BCM2710)
#define MAILBOX_BASE        0x3F00B880

#define MAILBOX_READ        (*(volatile uint32_t *)(MAILBOX_BASE + 0x00))
#define MAILBOX_STATUS      (*(volatile uint32_t *)(MAILBOX_BASE + 0x18))
#define MAILBOX_WRITE       (*(volatile uint32_t *)(MAILBOX_BASE + 0x20))

#define MAILBOX_FULL        0x80000000
#define MAILBOX_EMPTY       0x40000000

// Mailbox channels
#define MAILBOX_CH_PROP     8   // Property tags (ARM -> VC)

// Property tags
#define TAG_END             0x00000000
#define TAG_SET_PHYS_WH     0x00048003  // Set physical display width/height
#define TAG_SET_VIRT_WH     0x00048004  // Set virtual display width/height
#define TAG_SET_DEPTH       0x00048005  // Set bits per pixel
#define TAG_SET_PIXEL_ORDER 0x00048006  // Set pixel order (RGB vs BGR)
#define TAG_ALLOCATE_FB     0x00040001  // Allocate framebuffer
#define TAG_GET_PITCH       0x00040008  // Get bytes per row

// Must be 16-byte aligned for mailbox
static volatile uint32_t __attribute__((aligned(16))) mailbox_buffer[36];

// Memory barrier
static inline void dmb(void) {
    asm volatile("dmb sy" ::: "memory");
}

// Debug output (works before printf is available)
static void debug_putc(char c) {
    // Use UART for early debug - Mini UART at 0x3F215040
    // Note: UART must be initialized first, or this does nothing
    volatile uint32_t *uart_io = (volatile uint32_t *)0x3F215040;
    volatile uint32_t *uart_lsr = (volatile uint32_t *)0x3F215054;

    // Wait for TX ready (bit 5 of LSR)
    while (!(*uart_lsr & 0x20));
    *uart_io = c;
}

static void debug_puts(const char *s) {
    while (*s) {
        if (*s == '\n') debug_putc('\r');
        debug_putc(*s++);
    }
}

static void debug_hex(uint32_t val) {
    const char *hex = "0123456789ABCDEF";
    debug_puts("0x");
    for (int i = 28; i >= 0; i -= 4) {
        debug_putc(hex[(val >> i) & 0xF]);
    }
}

// Write to mailbox
static void mailbox_write(uint32_t channel, uint32_t data) {
    // Wait until mailbox is not full
    while (MAILBOX_STATUS & MAILBOX_FULL) {
        dmb();
    }
    dmb();

    // Write address (upper 28 bits) | channel (lower 4 bits)
    MAILBOX_WRITE = (data & 0xFFFFFFF0) | (channel & 0xF);
    dmb();
}

// Read from mailbox
static uint32_t mailbox_read(uint32_t channel) {
    uint32_t data;

    while (1) {
        // Wait until mailbox is not empty
        while (MAILBOX_STATUS & MAILBOX_EMPTY) {
            dmb();
        }
        dmb();

        data = MAILBOX_READ;
        dmb();

        // Check if it's for our channel
        if ((data & 0xF) == channel) {
            return data & 0xFFFFFFF0;
        }
    }
}

// Convert ARM physical address to bus address for GPU
// On Pi Zero 2W: bus address = phys + 0xC0000000 (for cached)
// Or: bus address = phys + 0x40000000 (for uncached - what we want for mailbox)
static uint32_t arm_to_bus(void *ptr) {
    return ((uint32_t)(uint64_t)ptr) | 0xC0000000;
}

// Convert bus address to ARM physical
static void *bus_to_arm(uint32_t bus) {
    return (void *)(uint64_t)(bus & 0x3FFFFFFF);
}

int hal_fb_init(uint32_t width, uint32_t height) {
    debug_puts("[HAL/FB] Pi framebuffer init\n");

    // Build property message
    // Must be 16-byte aligned, and we pass the bus address
    uint32_t idx = 0;

    mailbox_buffer[idx++] = 0;              // Total size (filled in later)
    mailbox_buffer[idx++] = 0;              // Request code

    // Set physical display size
    mailbox_buffer[idx++] = TAG_SET_PHYS_WH;
    mailbox_buffer[idx++] = 8;              // Value buffer size
    mailbox_buffer[idx++] = 0;              // Request/response code
    mailbox_buffer[idx++] = width;          // Width
    mailbox_buffer[idx++] = height;         // Height

    // Set virtual display size (same as physical)
    mailbox_buffer[idx++] = TAG_SET_VIRT_WH;
    mailbox_buffer[idx++] = 8;
    mailbox_buffer[idx++] = 0;
    mailbox_buffer[idx++] = width;
    mailbox_buffer[idx++] = height;

    // Set depth (bits per pixel)
    mailbox_buffer[idx++] = TAG_SET_DEPTH;
    mailbox_buffer[idx++] = 4;
    mailbox_buffer[idx++] = 0;
    mailbox_buffer[idx++] = 32;             // 32 bits per pixel

    // Set pixel order (0 = BGR, 1 = RGB) - we want RGB
    mailbox_buffer[idx++] = TAG_SET_PIXEL_ORDER;
    mailbox_buffer[idx++] = 4;
    mailbox_buffer[idx++] = 0;
    mailbox_buffer[idx++] = 1;              // RGB

    // Allocate framebuffer
    mailbox_buffer[idx++] = TAG_ALLOCATE_FB;
    mailbox_buffer[idx++] = 8;
    mailbox_buffer[idx++] = 0;
    mailbox_buffer[idx++] = 4096;           // Alignment (4K)
    mailbox_buffer[idx++] = 0;              // Will be filled with size

    // Get pitch
    mailbox_buffer[idx++] = TAG_GET_PITCH;
    mailbox_buffer[idx++] = 4;
    mailbox_buffer[idx++] = 0;
    mailbox_buffer[idx++] = 0;              // Will be filled with pitch

    // End tag
    mailbox_buffer[idx++] = TAG_END;

    // Set total size (in bytes)
    mailbox_buffer[0] = idx * 4;

    debug_puts("[HAL/FB] Sending mailbox request...\n");

    // Send message to GPU
    dmb();
    uint32_t bus_addr = arm_to_bus((void *)mailbox_buffer);
    mailbox_write(MAILBOX_CH_PROP, bus_addr);

    // Wait for response
    mailbox_read(MAILBOX_CH_PROP);
    dmb();

    // Check response
    if (mailbox_buffer[1] != 0x80000000) {
        debug_puts("[HAL/FB] ERROR: Mailbox request failed! Code: ");
        debug_hex(mailbox_buffer[1]);
        debug_puts("\n");
        return -1;
    }

    debug_puts("[HAL/FB] Mailbox success!\n");

    // Parse response - find the allocate framebuffer response
    idx = 2;
    uint32_t fb_addr = 0;
    uint32_t fb_size = 0;
    uint32_t pitch = 0;

    while (mailbox_buffer[idx] != TAG_END) {
        uint32_t tag = mailbox_buffer[idx++];
        uint32_t size = mailbox_buffer[idx++];
        idx++;  // Skip request/response code

        if (tag == TAG_ALLOCATE_FB) {
            fb_addr = mailbox_buffer[idx];
            fb_size = mailbox_buffer[idx + 1];
        } else if (tag == TAG_GET_PITCH) {
            pitch = mailbox_buffer[idx];
        }

        idx += (size + 3) / 4;  // Move to next tag (size is in bytes, round up)
    }

    if (fb_addr == 0) {
        debug_puts("[HAL/FB] ERROR: No framebuffer allocated!\n");
        return -1;
    }

    // Convert bus address to ARM address
    fb_info.base = (uint32_t *)bus_to_arm(fb_addr);
    fb_info.width = width;
    fb_info.height = height;
    fb_info.pitch = pitch;

    debug_puts("[HAL/FB] Framebuffer at: ");
    debug_hex((uint32_t)(uint64_t)fb_info.base);
    debug_puts("\n");
    debug_puts("[HAL/FB] Size: ");
    debug_hex(fb_size);
    debug_puts("\n");
    debug_puts("[HAL/FB] Pitch: ");
    debug_hex(pitch);
    debug_puts("\n");

    // Clear to a visible color (blue) so we know it works
    for (uint32_t i = 0; i < width * height; i++) {
        fb_info.base[i] = 0x000000FF;  // Blue
    }

    debug_puts("[HAL/FB] Pi framebuffer ready!\n");
    return 0;
}

hal_fb_info_t *hal_fb_get_info(void) {
    return &fb_info;
}
