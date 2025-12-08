/*
 * VibeOS Graphics Library
 *
 * Common drawing primitives for GUI applications.
 * Works with any buffer - desktop backbuffer, window buffers, etc.
 */

#ifndef GFX_H
#define GFX_H

#include "vibe.h"

// Graphics context - describes a drawing target
typedef struct {
    uint32_t *buffer;      // Pixel buffer
    int width;             // Buffer width in pixels
    int height;            // Buffer height in pixels
    const uint8_t *font;   // Font data (from kapi->font_data)
} gfx_ctx_t;

// Initialize a graphics context
static inline void gfx_init(gfx_ctx_t *ctx, uint32_t *buffer, int w, int h, const uint8_t *font) {
    ctx->buffer = buffer;
    ctx->width = w;
    ctx->height = h;
    ctx->font = font;
}

// ============ Basic Drawing Primitives ============

// Put a single pixel
static inline void gfx_put_pixel(gfx_ctx_t *ctx, int x, int y, uint32_t color) {
    if (x >= 0 && x < ctx->width && y >= 0 && y < ctx->height) {
        ctx->buffer[y * ctx->width + x] = color;
    }
}

// Fill a rectangle with solid color
static inline void gfx_fill_rect(gfx_ctx_t *ctx, int x, int y, int w, int h, uint32_t color) {
    for (int py = y; py < y + h && py < ctx->height; py++) {
        if (py < 0) continue;
        for (int px = x; px < x + w && px < ctx->width; px++) {
            if (px < 0) continue;
            ctx->buffer[py * ctx->width + px] = color;
        }
    }
}

// Draw a horizontal line
static inline void gfx_draw_hline(gfx_ctx_t *ctx, int x, int y, int w, uint32_t color) {
    if (y < 0 || y >= ctx->height) return;
    for (int i = 0; i < w; i++) {
        int px = x + i;
        if (px >= 0 && px < ctx->width) {
            ctx->buffer[y * ctx->width + px] = color;
        }
    }
}

// Draw a vertical line
static inline void gfx_draw_vline(gfx_ctx_t *ctx, int x, int y, int h, uint32_t color) {
    if (x < 0 || x >= ctx->width) return;
    for (int i = 0; i < h; i++) {
        int py = y + i;
        if (py >= 0 && py < ctx->height) {
            ctx->buffer[py * ctx->width + x] = color;
        }
    }
}

// Draw a rectangle outline
static inline void gfx_draw_rect(gfx_ctx_t *ctx, int x, int y, int w, int h, uint32_t color) {
    gfx_draw_hline(ctx, x, y, w, color);
    gfx_draw_hline(ctx, x, y + h - 1, w, color);
    gfx_draw_vline(ctx, x, y, h, color);
    gfx_draw_vline(ctx, x + w - 1, y, h, color);
}

// ============ Text Drawing ============

// Draw a single character (8x16 font)
static inline void gfx_draw_char(gfx_ctx_t *ctx, int x, int y, char c, uint32_t fg, uint32_t bg) {
    const uint8_t *glyph = &ctx->font[(unsigned char)c * 16];
    for (int row = 0; row < 16; row++) {
        for (int col = 0; col < 8; col++) {
            uint32_t color = (glyph[row] & (0x80 >> col)) ? fg : bg;
            int px = x + col;
            int py = y + row;
            if (px >= 0 && px < ctx->width && py >= 0 && py < ctx->height) {
                ctx->buffer[py * ctx->width + px] = color;
            }
        }
    }
}

// Draw a string
static inline void gfx_draw_string(gfx_ctx_t *ctx, int x, int y, const char *s, uint32_t fg, uint32_t bg) {
    while (*s) {
        gfx_draw_char(ctx, x, y, *s, fg, bg);
        x += 8;
        s++;
    }
}

// Draw a string with clipping (max width in pixels)
static inline void gfx_draw_string_clip(gfx_ctx_t *ctx, int x, int y, const char *s, uint32_t fg, uint32_t bg, int max_w) {
    int drawn = 0;
    while (*s && drawn + 8 <= max_w) {
        gfx_draw_char(ctx, x, y, *s, fg, bg);
        x += 8;
        drawn += 8;
        s++;
    }
}

// ============ Patterns (for desktop background, etc.) ============

// Classic Mac diagonal checkerboard pattern
static inline void gfx_fill_pattern(gfx_ctx_t *ctx, int x, int y, int w, int h, uint32_t c1, uint32_t c2) {
    for (int py = y; py < y + h && py < ctx->height; py++) {
        if (py < 0) continue;
        for (int px = x; px < x + w && px < ctx->width; px++) {
            if (px < 0) continue;
            int pattern = ((px + py) % 2 == 0) ? 1 : 0;
            ctx->buffer[py * ctx->width + px] = pattern ? c1 : c2;
        }
    }
}

// 25% dither pattern (sparse dots)
static inline void gfx_fill_dither25(gfx_ctx_t *ctx, int x, int y, int w, int h, uint32_t c1, uint32_t c2) {
    for (int py = y; py < y + h && py < ctx->height; py++) {
        if (py < 0) continue;
        for (int px = x; px < x + w && px < ctx->width; px++) {
            if (px < 0) continue;
            int pattern = ((px % 2 == 0) && (py % 2 == 0)) ? 1 : 0;
            ctx->buffer[py * ctx->width + px] = pattern ? c1 : c2;
        }
    }
}

#endif // GFX_H
