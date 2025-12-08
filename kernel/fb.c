/*
 * VibeOS Framebuffer Driver
 *
 * Uses QEMU ramfb device via fw_cfg interface (non-DMA)
 */

#include "fb.h"
#include "printf.h"
#include "string.h"
#include "memory.h"

// Framebuffer state
uint32_t fb_width = 0;
uint32_t fb_height = 0;
uint32_t fb_pitch = 0;
uint32_t *fb_base = NULL;

// QEMU fw_cfg MMIO interface (for aarch64 virt machine)
#define FW_CFG_BASE         0x09020000
#define FW_CFG_DATA8        (*(volatile uint8_t *)(FW_CFG_BASE + 0x00))
#define FW_CFG_DATA16       (*(volatile uint16_t *)(FW_CFG_BASE + 0x00))
#define FW_CFG_DATA32       (*(volatile uint32_t *)(FW_CFG_BASE + 0x00))
#define FW_CFG_DATA64       (*(volatile uint64_t *)(FW_CFG_BASE + 0x00))
#define FW_CFG_SELECTOR     (*(volatile uint16_t *)(FW_CFG_BASE + 0x08))
#define FW_CFG_DMA_ADDR_HI  (*(volatile uint32_t *)(FW_CFG_BASE + 0x10))
#define FW_CFG_DMA_ADDR_LO  (*(volatile uint32_t *)(FW_CFG_BASE + 0x14))

// fw_cfg selectors
#define FW_CFG_SIGNATURE    0x0000
#define FW_CFG_FILE_DIR     0x0019

// ramfb configuration structure (big-endian!)
typedef struct __attribute__((packed)) {
    uint64_t addr;
    uint32_t fourcc;
    uint32_t flags;
    uint32_t width;
    uint32_t height;
    uint32_t stride;
} ramfb_config_t;

// Byte swap helpers for big-endian fw_cfg
static uint16_t bswap16(uint16_t x) {
    return (x >> 8) | (x << 8);
}

static uint32_t bswap32(uint32_t x) {
    return ((x >> 24) & 0xff) | ((x >> 8) & 0xff00) |
           ((x << 8) & 0xff0000) | ((x << 24) & 0xff000000);
}

static uint64_t bswap64(uint64_t x) {
    return ((uint64_t)bswap32(x & 0xFFFFFFFF) << 32) | bswap32(x >> 32);
}

// DMA control structure
typedef struct __attribute__((packed)) {
    uint32_t control;
    uint32_t length;
    uint64_t address;
} fw_cfg_dma_t;

#define FW_CFG_DMA_CTL_ERROR  0x01
#define FW_CFG_DMA_CTL_READ   0x02
#define FW_CFG_DMA_CTL_SKIP   0x04
#define FW_CFG_DMA_CTL_SELECT 0x08
#define FW_CFG_DMA_CTL_WRITE  0x10

// File directory entry
typedef struct __attribute__((packed)) {
    uint32_t size;
    uint16_t select;
    uint16_t reserved;
    char name[56];
} fw_cfg_file_t;

// Read bytes from current fw_cfg selection
static void fw_cfg_read(void *buf, uint32_t len) {
    uint8_t *p = (uint8_t *)buf;
    for (uint32_t i = 0; i < len; i++) {
        p[i] = FW_CFG_DATA8;
    }
}

// Write bytes via DMA
static void fw_cfg_write_dma(uint16_t selector, void *buf, uint32_t len) {
    volatile fw_cfg_dma_t dma __attribute__((aligned(16)));

    dma.control = bswap32(FW_CFG_DMA_CTL_SELECT | FW_CFG_DMA_CTL_WRITE | ((uint32_t)selector << 16));
    dma.length = bswap32(len);
    dma.address = bswap64((uint64_t)buf);

    uint64_t dma_addr = (uint64_t)&dma;

    // Memory barrier
    asm volatile("dsb sy" ::: "memory");

    // Write DMA address (big-endian, high word first)
    FW_CFG_DMA_ADDR_HI = bswap32((uint32_t)(dma_addr >> 32));
    FW_CFG_DMA_ADDR_LO = bswap32((uint32_t)(dma_addr & 0xFFFFFFFF));

    // Wait for completion
    while (bswap32(dma.control) & ~FW_CFG_DMA_CTL_ERROR) {
        asm volatile("dsb sy" ::: "memory");
    }
}

static int find_ramfb_selector(void) {
    // Select file directory - selector is written as little-endian on MMIO
    FW_CFG_SELECTOR = bswap16(FW_CFG_FILE_DIR);

    // Small delay for selector to take effect
    for (volatile int i = 0; i < 1000; i++);

    // Read file count (big-endian in the data)
    uint32_t count;
    fw_cfg_read(&count, sizeof(count));
    count = bswap32(count);

    printf("[FB] fw_cfg has %d files\n", count);

    if (count == 0 || count > 100) {
        // Try without byteswap on selector
        FW_CFG_SELECTOR = FW_CFG_FILE_DIR;
        for (volatile int i = 0; i < 1000; i++);
        fw_cfg_read(&count, sizeof(count));
        count = bswap32(count);
        printf("[FB] Retry: fw_cfg has %d files\n", count);
    }

    if (count == 0 || count > 100) {
        printf("[FB] ERROR: Unreasonable file count\n");
        return -1;
    }

    // Search for "etc/ramfb"
    for (uint32_t i = 0; i < count; i++) {
        fw_cfg_file_t file;
        fw_cfg_read(&file, sizeof(file));

        if (strcmp(file.name, "etc/ramfb") == 0) {
            uint16_t sel = bswap16(file.select);
            printf("[FB] Found ramfb: select=0x%x size=%d\n", sel, bswap32(file.size));
            return sel;
        }
    }

    return -1;
}

int fb_init(void) {
    printf("[FB] Initializing framebuffer...\n");

    // Find ramfb config selector
    int selector = find_ramfb_selector();
    if (selector < 0) {
        printf("[FB] ERROR: ramfb device not found!\n");
        return -1;
    }

    // Set up our desired resolution
    fb_width = 800;
    fb_height = 600;
    fb_pitch = fb_width * 4;  // 4 bytes per pixel (32-bit)

    // Allocate framebuffer from heap
    // Size = 800 * 600 * 4 = 1,920,000 bytes (~1.9 MB)
    size_t fb_size = fb_width * fb_height * sizeof(uint32_t);
    fb_base = (uint32_t *)malloc(fb_size);
    if (!fb_base) {
        printf("[FB] ERROR: Failed to allocate framebuffer!\n");
        return -1;
    }

    // Configure ramfb (all values big-endian)
    ramfb_config_t config;
    config.addr = bswap64((uint64_t)fb_base);
    config.fourcc = bswap32(0x34325258);  // "XR24" = XRGB8888
    config.flags = bswap32(0);
    config.width = bswap32(fb_width);
    config.height = bswap32(fb_height);
    config.stride = bswap32(fb_pitch);

    // Write config via DMA
    fw_cfg_write_dma((uint16_t)selector, &config, sizeof(config));

    printf("[FB] Configured: %dx%d @ %p\n", fb_width, fb_height, fb_base);

    // Clear to black
    fb_clear(COLOR_BLACK);

    printf("[FB] Framebuffer ready!\n");
    return 0;
}

void fb_put_pixel(uint32_t x, uint32_t y, uint32_t color) {
    if (x >= fb_width || y >= fb_height) return;
    fb_base[y * fb_width + x] = color;
}

void fb_fill_rect(uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint32_t color) {
    for (uint32_t row = y; row < y + h && row < fb_height; row++) {
        for (uint32_t col = x; col < x + w && col < fb_width; col++) {
            fb_base[row * fb_width + col] = color;
        }
    }
}

void fb_clear(uint32_t color) {
    for (uint32_t i = 0; i < fb_width * fb_height; i++) {
        fb_base[i] = color;
    }
}

// Include font data
#include "font.h"

void fb_draw_char(uint32_t x, uint32_t y, char c, uint32_t fg, uint32_t bg) {
    const uint8_t *glyph = font_data[(uint8_t)c];

    for (int row = 0; row < FONT_HEIGHT; row++) {
        uint8_t bits = glyph[row];
        for (int col = 0; col < FONT_WIDTH; col++) {
            uint32_t color = (bits & (0x80 >> col)) ? fg : bg;
            fb_put_pixel(x + col, y + row, color);
        }
    }
}

void fb_draw_string(uint32_t x, uint32_t y, const char *s, uint32_t fg, uint32_t bg) {
    uint32_t orig_x = x;
    while (*s) {
        if (*s == '\n') {
            x = orig_x;
            y += FONT_HEIGHT;
        } else {
            fb_draw_char(x, y, *s, fg, bg);
            x += FONT_WIDTH;
        }
        s++;
    }
}
