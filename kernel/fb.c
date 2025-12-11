/*
 * VibeOS Framebuffer Driver
 *
 * Generic framebuffer operations.
 * Platform-specific initialization is in hal/<platform>/fb.c
 */

#include "fb.h"
#include "printf.h"
#include "string.h"
#include "hal/hal.h"

// Framebuffer state - these are exported for backward compatibility
uint32_t fb_width = 0;
uint32_t fb_height = 0;
uint32_t fb_pitch = 0;
uint32_t *fb_base = NULL;

int fb_init(void) {
    printf("[FB] Initializing framebuffer...\n");

    // Call platform-specific init
    if (hal_fb_init(1024, 768) < 0) {
        printf("[FB] ERROR: Platform FB init failed!\n");
        return -1;
    }

    // Get info from HAL
    hal_fb_info_t *info = hal_fb_get_info();
    if (!info || !info->base) {
        printf("[FB] ERROR: No framebuffer info!\n");
        return -1;
    }

    // Copy to our global vars for backward compatibility
    fb_base = info->base;
    fb_width = info->width;
    fb_height = info->height;
    fb_pitch = info->pitch;

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
