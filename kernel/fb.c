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
    // Note: Don't use printf here - console isn't initialized yet!

    // Call platform-specific init with 1080p resolution
    if (hal_fb_init(1920, 1080) < 0) {
        return -1;
    }

    // Get info from HAL
    hal_fb_info_t *info = hal_fb_get_info();
    if (!info || !info->base) {
        return -1;
    }

    // Copy to our global vars for backward compatibility
    fb_base = info->base;
    fb_width = info->width;
    fb_height = info->height;
    fb_pitch = info->pitch;

    // Clear to black
    fb_clear(COLOR_BLACK);

    return 0;
}

void fb_put_pixel(uint32_t x, uint32_t y, uint32_t color) {
    if (x >= fb_width || y >= fb_height) return;
    fb_base[y * fb_width + x] = color;
}

void fb_fill_rect(uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint32_t color) {
    // Clip to screen bounds
    if (x >= fb_width || y >= fb_height) return;
    if (x + w > fb_width) w = fb_width - x;
    if (y + h > fb_height) h = fb_height - y;

    // Fill row by row using memset32
    for (uint32_t row = y; row < y + h; row++) {
        memset32(&fb_base[row * fb_width + x], color, w);
    }
}

void fb_clear(uint32_t color) {
    memset32(fb_base, color, fb_width * fb_height);
}

// Include font data
#include "font.h"

void fb_draw_char(uint32_t x, uint32_t y, char c, uint32_t fg, uint32_t bg) {
    // Quick bounds check for entire character
    if (x + FONT_WIDTH > fb_width || y + FONT_HEIGHT > fb_height) return;

    const uint8_t *glyph = font_data[(uint8_t)c];
    uint32_t *row_ptr = &fb_base[y * fb_width + x];

    for (int row = 0; row < FONT_HEIGHT; row++) {
        uint8_t bits = glyph[row];
        // Unroll the 8-pixel row for speed
        row_ptr[0] = (bits & 0x80) ? fg : bg;
        row_ptr[1] = (bits & 0x40) ? fg : bg;
        row_ptr[2] = (bits & 0x20) ? fg : bg;
        row_ptr[3] = (bits & 0x10) ? fg : bg;
        row_ptr[4] = (bits & 0x08) ? fg : bg;
        row_ptr[5] = (bits & 0x04) ? fg : bg;
        row_ptr[6] = (bits & 0x02) ? fg : bg;
        row_ptr[7] = (bits & 0x01) ? fg : bg;
        row_ptr += fb_width;
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
