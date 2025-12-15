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

// Fill a rectangle with solid color (optimized with 64-bit stores)
static inline void gfx_fill_rect(gfx_ctx_t *ctx, int x, int y, int w, int h, uint32_t color) {
    // Clip to bounds
    if (x < 0) { w += x; x = 0; }
    if (y < 0) { h += y; y = 0; }
    if (x + w > ctx->width) w = ctx->width - x;
    if (y + h > ctx->height) h = ctx->height - y;
    if (w <= 0 || h <= 0) return;

    // Fill row by row using fast 64-bit memset
    for (int py = y; py < y + h; py++) {
        memset32_fast(&ctx->buffer[py * ctx->width + x], color, w);
    }
}

// Draw a horizontal line (optimized with 64-bit stores)
static inline void gfx_draw_hline(gfx_ctx_t *ctx, int x, int y, int w, uint32_t color) {
    if (y < 0 || y >= ctx->height) return;
    // Clip to bounds
    if (x < 0) { w += x; x = 0; }
    if (x + w > ctx->width) w = ctx->width - x;
    if (w <= 0) return;
    memset32_fast(&ctx->buffer[y * ctx->width + x], color, w);
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

// ============ TTF Text Drawing ============

// Draw a TTF glyph (grayscale antialiased)
// The glyph bitmap is grayscale 0-255, we blend with background
static inline void gfx_draw_ttf_glyph(gfx_ctx_t *ctx, int x, int y, ttf_glyph_t *glyph, uint32_t fg, uint32_t bg) {
    if (!glyph || !glyph->bitmap) return;

    // Apply glyph offsets
    x += glyph->xoff;
    y += glyph->yoff;

    uint8_t fg_r = (fg >> 16) & 0xFF;
    uint8_t fg_g = (fg >> 8) & 0xFF;
    uint8_t fg_b = fg & 0xFF;
    uint8_t bg_r = (bg >> 16) & 0xFF;
    uint8_t bg_g = (bg >> 8) & 0xFF;
    uint8_t bg_b = bg & 0xFF;

    for (int row = 0; row < glyph->height; row++) {
        for (int col = 0; col < glyph->width; col++) {
            int px = x + col;
            int py = y + row;
            if (px < 0 || px >= ctx->width || py < 0 || py >= ctx->height) continue;

            uint8_t alpha = glyph->bitmap[row * glyph->width + col];
            if (alpha == 0) continue;  // Fully transparent

            if (alpha == 255) {
                // Fully opaque
                ctx->buffer[py * ctx->width + px] = fg;
            } else {
                // Blend
                uint8_t r = (fg_r * alpha + bg_r * (255 - alpha)) / 255;
                uint8_t g = (fg_g * alpha + bg_g * (255 - alpha)) / 255;
                uint8_t b = (fg_b * alpha + bg_b * (255 - alpha)) / 255;
                ctx->buffer[py * ctx->width + px] = (r << 16) | (g << 8) | b;
            }
        }
    }
}

// Draw a TTF string at given size and style
// Returns the width of the drawn string in pixels
static inline int gfx_draw_ttf_string(gfx_ctx_t *ctx, kapi_t *k, int x, int y,
                                       const char *s, int size, int style,
                                       uint32_t fg, uint32_t bg) {
    if (!k->ttf_is_ready || !k->ttf_is_ready()) {
        // Fallback to bitmap font
        gfx_draw_string(ctx, x, y, s, fg, bg);
        return strlen(s) * 8;
    }

    // Get font metrics for baseline
    int ascent, descent, line_gap;
    k->ttf_get_metrics(size, &ascent, &descent, &line_gap);

    int start_x = x;
    int prev_cp = 0;

    while (*s) {
        int cp = (unsigned char)*s;

        // Add kerning
        if (prev_cp) {
            x += k->ttf_get_kerning(prev_cp, cp, size);
        }

        ttf_glyph_t *glyph = (ttf_glyph_t *)k->ttf_get_glyph(cp, size, style);
        if (glyph) {
            // y + ascent puts the baseline at y + ascent
            // glyph->yoff is relative to baseline (negative = above)
            gfx_draw_ttf_glyph(ctx, x, y + ascent, glyph, fg, bg);
            x += glyph->advance;
        } else {
            x += size / 2;  // Default advance for missing glyph
        }

        prev_cp = cp;
        s++;
    }

    return x - start_x;
}

// ============ Patterns (for desktop background, etc.) ============

// Classic Mac diagonal checkerboard pattern (optimized with 64-bit stores)
static inline void gfx_fill_pattern(gfx_ctx_t *ctx, int x, int y, int w, int h, uint32_t c1, uint32_t c2) {
    // Clip to bounds
    if (x < 0) { w += x; x = 0; }
    if (y < 0) { h += y; y = 0; }
    if (x + w > ctx->width) w = ctx->width - x;
    if (y + h > ctx->height) h = ctx->height - y;
    if (w <= 0 || h <= 0) return;

    // Precompute 64-bit patterns for two pixels at a time
    // Pattern alternates c1,c2 or c2,c1 depending on row parity
    uint64_t pattern_even = ((uint64_t)c2 << 32) | c1;  // c1 at even x, c2 at odd x
    uint64_t pattern_odd = ((uint64_t)c1 << 32) | c2;   // c2 at even x, c1 at odd x

    for (int py = y; py < y + h; py++) {
        uint32_t *row = &ctx->buffer[py * ctx->width + x];
        int row_parity = (py + x) & 1;  // Determines starting color

        // Use 64-bit stores for pairs of pixels
        if ((x & 1) == 0 && w >= 2) {
            uint64_t *row64 = (uint64_t *)row;
            uint64_t p = row_parity ? pattern_odd : pattern_even;
            int pairs = w / 2;
            for (int i = 0; i < pairs; i++) {
                row64[i] = p;
            }
            // Handle odd pixel at end
            if (w & 1) {
                row[w - 1] = ((py + x + w - 1) & 1) ? c2 : c1;
            }
        } else {
            // Fallback for unaligned start
            for (int px = 0; px < w; px++) {
                row[px] = ((px + x + py) & 1) ? c2 : c1;
            }
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
