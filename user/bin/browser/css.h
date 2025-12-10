/*
 * CSS Parser and Style Engine for VibeOS Browser
 *
 * Supports:
 * - Inline styles (style="...")
 * - <style> blocks
 * - Basic selectors (tag, .class, #id, combinators)
 * - Common properties (display, width, margin, padding, color, etc.)
 */
#ifndef BROWSER_CSS_H
#define BROWSER_CSS_H

#include "str.h"
#include "../../lib/vibe.h"

// Forward declare kapi
static kapi_t *css_kapi = NULL;

static inline void css_set_kapi(kapi_t *k) {
    css_kapi = k;
}

// ============ CSS Values ============

typedef enum {
    CSS_UNIT_NONE,
    CSS_UNIT_PX,
    CSS_UNIT_EM,
    CSS_UNIT_REM,
    CSS_UNIT_PERCENT,
    CSS_UNIT_AUTO
} css_unit_t;

typedef struct {
    float value;
    css_unit_t unit;
} css_length_t;

typedef enum {
    CSS_DISPLAY_INLINE,
    CSS_DISPLAY_BLOCK,
    CSS_DISPLAY_INLINE_BLOCK,
    CSS_DISPLAY_NONE,
    CSS_DISPLAY_TABLE,
    CSS_DISPLAY_TABLE_ROW,
    CSS_DISPLAY_TABLE_CELL,
    CSS_DISPLAY_FLEX,
    CSS_DISPLAY_LIST_ITEM
} css_display_t;

typedef enum {
    CSS_FLOAT_NONE,
    CSS_FLOAT_LEFT,
    CSS_FLOAT_RIGHT
} css_float_t;

typedef enum {
    CSS_POSITION_STATIC,
    CSS_POSITION_RELATIVE,
    CSS_POSITION_ABSOLUTE,
    CSS_POSITION_FIXED
} css_position_t;

typedef enum {
    CSS_TEXT_ALIGN_LEFT,
    CSS_TEXT_ALIGN_CENTER,
    CSS_TEXT_ALIGN_RIGHT,
    CSS_TEXT_ALIGN_JUSTIFY
} css_text_align_t;

typedef enum {
    CSS_FONT_WEIGHT_NORMAL,
    CSS_FONT_WEIGHT_BOLD
} css_font_weight_t;

typedef enum {
    CSS_FONT_STYLE_NORMAL,
    CSS_FONT_STYLE_ITALIC
} css_font_style_t;

typedef enum {
    CSS_TEXT_DECORATION_NONE,
    CSS_TEXT_DECORATION_UNDERLINE,
    CSS_TEXT_DECORATION_LINE_THROUGH
} css_text_decoration_t;

typedef enum {
    CSS_VISIBILITY_VISIBLE,
    CSS_VISIBILITY_HIDDEN
} css_visibility_t;

typedef enum {
    CSS_WHITE_SPACE_NORMAL,
    CSS_WHITE_SPACE_PRE,
    CSS_WHITE_SPACE_NOWRAP,
    CSS_WHITE_SPACE_PRE_WRAP
} css_white_space_t;

typedef enum {
    CSS_VERTICAL_ALIGN_BASELINE,
    CSS_VERTICAL_ALIGN_TOP,
    CSS_VERTICAL_ALIGN_MIDDLE,
    CSS_VERTICAL_ALIGN_BOTTOM,
    CSS_VERTICAL_ALIGN_SUB,
    CSS_VERTICAL_ALIGN_SUPER
} css_vertical_align_t;

// ============ Computed Style ============

// Flags for which properties are set
#define CSS_PROP_DISPLAY        (1 << 0)
#define CSS_PROP_WIDTH          (1 << 1)
#define CSS_PROP_HEIGHT         (1 << 2)
#define CSS_PROP_MARGIN         (1 << 3)
#define CSS_PROP_PADDING        (1 << 4)
#define CSS_PROP_COLOR          (1 << 5)
#define CSS_PROP_BG_COLOR       (1 << 6)
#define CSS_PROP_FONT_SIZE      (1 << 7)
#define CSS_PROP_FONT_WEIGHT    (1 << 8)
#define CSS_PROP_FONT_STYLE     (1 << 9)
#define CSS_PROP_TEXT_ALIGN     (1 << 10)
#define CSS_PROP_TEXT_DECORATION (1 << 11)
#define CSS_PROP_FLOAT          (1 << 12)
#define CSS_PROP_POSITION       (1 << 13)
#define CSS_PROP_VISIBILITY     (1 << 14)
#define CSS_PROP_WHITE_SPACE    (1 << 15)
#define CSS_PROP_VERTICAL_ALIGN (1 << 16)
#define CSS_PROP_BORDER_WIDTH   (1 << 17)
#define CSS_PROP_BORDER_COLOR   (1 << 18)
#define CSS_PROP_LIST_STYLE     (1 << 19)

typedef struct {
    uint32_t props_set;  // Bitmask of which properties are set

    // Display & layout
    css_display_t display;
    css_float_t float_prop;
    css_position_t position;
    css_visibility_t visibility;

    // Box model
    css_length_t width;
    css_length_t height;
    css_length_t margin_top, margin_right, margin_bottom, margin_left;
    css_length_t padding_top, padding_right, padding_bottom, padding_left;

    // Border
    css_length_t border_width;
    uint32_t border_color;

    // Colors
    uint32_t color;         // Text color (0xRRGGBB)
    uint32_t background_color;

    // Text
    css_length_t font_size;
    css_font_weight_t font_weight;
    css_font_style_t font_style;
    css_text_align_t text_align;
    css_text_decoration_t text_decoration;
    css_white_space_t white_space;
    css_vertical_align_t vertical_align;

    // List
    int list_style_type;  // 0 = none, 1 = disc, 2 = decimal, etc.
} css_style_t;

// ============ Selector ============

typedef enum {
    SEL_TAG,        // div
    SEL_CLASS,      // .foo
    SEL_ID,         // #bar
    SEL_UNIVERSAL,  // *
    SEL_ATTRIBUTE   // [attr] or [attr=value]
} selector_type_t;

typedef enum {
    COMB_NONE,        // Single selector
    COMB_DESCENDANT,  // A B (space)
    COMB_CHILD,       // A > B
    COMB_ADJACENT,    // A + B
    COMB_SIBLING      // A ~ B
} combinator_t;

// A simple selector part (e.g., "div" or ".class" or "#id")
typedef struct {
    selector_type_t type;
    char name[64];        // Tag name, class name, or id
    char attr_name[32];   // For attribute selectors
    char attr_value[64];  // For attribute selectors
} selector_part_t;

// A full selector can have multiple parts with combinators
#define MAX_SELECTOR_PARTS 8
typedef struct {
    selector_part_t parts[MAX_SELECTOR_PARTS];
    combinator_t combinators[MAX_SELECTOR_PARTS];  // Combinator before each part (first is COMB_NONE)
    int num_parts;
} css_selector_t;

// ============ CSS Rule ============

typedef struct css_rule {
    css_selector_t selector;
    css_style_t style;
    int specificity;  // Calculated specificity for cascade
    struct css_rule *next;
} css_rule_t;

// ============ Stylesheet ============

static css_rule_t *stylesheet_head = NULL;
static css_rule_t *stylesheet_tail = NULL;

// ============ Parsing Helpers ============

static inline void skip_whitespace(const char **p, const char *end) {
    while (*p < end && (**p == ' ' || **p == '\t' || **p == '\n' || **p == '\r')) {
        (*p)++;
    }
}

static inline void skip_whitespace_and_comments(const char **p, const char *end) {
    while (*p < end) {
        // Skip whitespace
        while (*p < end && (**p == ' ' || **p == '\t' || **p == '\n' || **p == '\r')) {
            (*p)++;
        }
        // Skip comments /* ... */
        if (*p + 1 < end && **p == '/' && *(*p + 1) == '*') {
            *p += 2;
            while (*p + 1 < end && !(**p == '*' && *(*p + 1) == '/')) {
                (*p)++;
            }
            if (*p + 1 < end) *p += 2;
        } else {
            break;
        }
    }
}

// Parse a CSS identifier (tag name, class name, property name, etc.)
static inline int parse_ident(const char **p, const char *end, char *out, int max_len) {
    const char *start = *p;
    int len = 0;

    // First char: letter, underscore, or hyphen (or escaped)
    if (*p < end && ((**p >= 'a' && **p <= 'z') || (**p >= 'A' && **p <= 'Z') ||
                     **p == '_' || **p == '-')) {
        out[len++] = **p;
        (*p)++;
    } else {
        return 0;
    }

    // Subsequent chars: letter, digit, underscore, hyphen
    while (*p < end && len < max_len - 1) {
        char c = **p;
        if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
            (c >= '0' && c <= '9') || c == '_' || c == '-') {
            out[len++] = c;
            (*p)++;
        } else {
            break;
        }
    }
    out[len] = '\0';
    return len;
}

// Parse a CSS number with optional unit
static inline css_length_t parse_length(const char **p, const char *end) {
    css_length_t result = {0, CSS_UNIT_NONE};

    skip_whitespace(p, end);

    // Check for "auto"
    if (*p + 4 <= end && str_ieqn(*p, "auto", 4)) {
        *p += 4;
        result.unit = CSS_UNIT_AUTO;
        return result;
    }

    // Parse number (including negative and decimal)
    int negative = 0;
    if (*p < end && **p == '-') {
        negative = 1;
        (*p)++;
    }

    float value = 0;
    int has_digits = 0;

    // Integer part
    while (*p < end && **p >= '0' && **p <= '9') {
        value = value * 10 + (**p - '0');
        (*p)++;
        has_digits = 1;
    }

    // Decimal part
    if (*p < end && **p == '.') {
        (*p)++;
        float decimal = 0.1f;
        while (*p < end && **p >= '0' && **p <= '9') {
            value += (**p - '0') * decimal;
            decimal *= 0.1f;
            (*p)++;
            has_digits = 1;
        }
    }

    if (!has_digits) return result;

    if (negative) value = -value;
    result.value = value;

    // Parse unit
    if (*p + 2 <= end && str_ieqn(*p, "px", 2)) {
        result.unit = CSS_UNIT_PX;
        *p += 2;
    } else if (*p + 2 <= end && str_ieqn(*p, "em", 2)) {
        result.unit = CSS_UNIT_EM;
        *p += 2;
    } else if (*p + 3 <= end && str_ieqn(*p, "rem", 3)) {
        result.unit = CSS_UNIT_REM;
        *p += 3;
    } else if (*p + 1 <= end && **p == '%') {
        result.unit = CSS_UNIT_PERCENT;
        *p += 1;
    } else if (value == 0) {
        // 0 without unit is valid
        result.unit = CSS_UNIT_PX;
    } else {
        // Assume px if no unit
        result.unit = CSS_UNIT_PX;
    }

    return result;
}

// Parse a color value
static inline uint32_t parse_color(const char **p, const char *end) {
    skip_whitespace(p, end);

    // Hex color: #RGB or #RRGGBB
    if (*p < end && **p == '#') {
        (*p)++;
        char hex[7] = {0};
        int len = 0;
        while (*p < end && len < 6) {
            char c = **p;
            if ((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F')) {
                hex[len++] = c;
                (*p)++;
            } else {
                break;
            }
        }

        uint32_t color = 0;
        if (len == 3) {
            // #RGB -> #RRGGBB
            for (int i = 0; i < 3; i++) {
                char c = hex[i];
                int val = (c >= '0' && c <= '9') ? c - '0' :
                          (c >= 'a' && c <= 'f') ? c - 'a' + 10 : c - 'A' + 10;
                color = (color << 8) | (val << 4) | val;
            }
        } else if (len == 6) {
            // #RRGGBB
            for (int i = 0; i < 6; i++) {
                char c = hex[i];
                int val = (c >= '0' && c <= '9') ? c - '0' :
                          (c >= 'a' && c <= 'f') ? c - 'a' + 10 : c - 'A' + 10;
                color = (color << 4) | val;
            }
        }
        return color;
    }

    // rgb(r, g, b) or rgba(r, g, b, a)
    if (*p + 4 <= end && (str_ieqn(*p, "rgb(", 4) || str_ieqn(*p, "rgba", 4))) {
        int is_rgba = str_ieqn(*p, "rgba", 4);
        *p += is_rgba ? 5 : 4;

        int r = 0, g = 0, b = 0;
        skip_whitespace(p, end);
        while (*p < end && **p >= '0' && **p <= '9') { r = r * 10 + (**p - '0'); (*p)++; }
        skip_whitespace(p, end);
        if (*p < end && **p == ',') (*p)++;
        skip_whitespace(p, end);
        while (*p < end && **p >= '0' && **p <= '9') { g = g * 10 + (**p - '0'); (*p)++; }
        skip_whitespace(p, end);
        if (*p < end && **p == ',') (*p)++;
        skip_whitespace(p, end);
        while (*p < end && **p >= '0' && **p <= '9') { b = b * 10 + (**p - '0'); (*p)++; }
        // Skip alpha and closing paren
        while (*p < end && **p != ')') (*p)++;
        if (*p < end) (*p)++;

        return ((r & 0xFF) << 16) | ((g & 0xFF) << 8) | (b & 0xFF);
    }

    // Named colors (common ones)
    struct { const char *name; uint32_t color; } named_colors[] = {
        {"black", 0x000000}, {"white", 0xFFFFFF}, {"red", 0xFF0000},
        {"green", 0x008000}, {"blue", 0x0000FF}, {"yellow", 0xFFFF00},
        {"cyan", 0x00FFFF}, {"magenta", 0xFF00FF}, {"gray", 0x808080},
        {"grey", 0x808080}, {"silver", 0xC0C0C0}, {"maroon", 0x800000},
        {"olive", 0x808000}, {"lime", 0x00FF00}, {"aqua", 0x00FFFF},
        {"teal", 0x008080}, {"navy", 0x000080}, {"fuchsia", 0xFF00FF},
        {"purple", 0x800080}, {"orange", 0xFFA500}, {"pink", 0xFFC0CB},
        {"brown", 0xA52A2A}, {"transparent", 0xFFFFFFFF},  // Special marker
        {NULL, 0}
    };

    for (int i = 0; named_colors[i].name; i++) {
        int len = str_len(named_colors[i].name);
        if (*p + len <= end && str_ieqn(*p, named_colors[i].name, len)) {
            // Check it's not part of a longer word
            char next = (*p)[len];
            if (next == ';' || next == ' ' || next == '\t' || next == '\n' ||
                next == ')' || next == '!' || next == '}' || next == '\0') {
                *p += len;
                return named_colors[i].color;
            }
        }
    }

    return 0x000000;  // Default to black
}

// ============ Property Parsing ============

static inline void parse_declaration(const char *prop, int prop_len,
                                     const char *value, int value_len,
                                     css_style_t *style) {
    // Lowercase the property name for comparison
    char prop_lower[64];
    for (int i = 0; i < prop_len && i < 63; i++) {
        char c = prop[i];
        prop_lower[i] = (c >= 'A' && c <= 'Z') ? c + 32 : c;
    }
    prop_lower[prop_len < 63 ? prop_len : 63] = '\0';

    const char *v = value;
    const char *v_end = value + value_len;
    skip_whitespace(&v, v_end);

    // Display
    if (str_eqn(prop_lower, "display", 7)) {
        style->props_set |= CSS_PROP_DISPLAY;
        if (str_ieqn(v, "none", 4)) style->display = CSS_DISPLAY_NONE;
        else if (str_ieqn(v, "block", 5)) style->display = CSS_DISPLAY_BLOCK;
        else if (str_ieqn(v, "inline-block", 12)) style->display = CSS_DISPLAY_INLINE_BLOCK;
        else if (str_ieqn(v, "inline", 6)) style->display = CSS_DISPLAY_INLINE;
        else if (str_ieqn(v, "flex", 4)) style->display = CSS_DISPLAY_FLEX;
        else if (str_ieqn(v, "table-cell", 10)) style->display = CSS_DISPLAY_TABLE_CELL;
        else if (str_ieqn(v, "table-row", 9)) style->display = CSS_DISPLAY_TABLE_ROW;
        else if (str_ieqn(v, "table", 5)) style->display = CSS_DISPLAY_TABLE;
        else if (str_ieqn(v, "list-item", 9)) style->display = CSS_DISPLAY_LIST_ITEM;
    }
    // Visibility
    else if (str_eqn(prop_lower, "visibility", 10)) {
        style->props_set |= CSS_PROP_VISIBILITY;
        if (str_ieqn(v, "hidden", 6)) style->visibility = CSS_VISIBILITY_HIDDEN;
        else style->visibility = CSS_VISIBILITY_VISIBLE;
    }
    // Float
    else if (str_eqn(prop_lower, "float", 5)) {
        style->props_set |= CSS_PROP_FLOAT;
        if (str_ieqn(v, "left", 4)) style->float_prop = CSS_FLOAT_LEFT;
        else if (str_ieqn(v, "right", 5)) style->float_prop = CSS_FLOAT_RIGHT;
        else style->float_prop = CSS_FLOAT_NONE;
    }
    // Position
    else if (str_eqn(prop_lower, "position", 8)) {
        style->props_set |= CSS_PROP_POSITION;
        if (str_ieqn(v, "relative", 8)) style->position = CSS_POSITION_RELATIVE;
        else if (str_ieqn(v, "absolute", 8)) style->position = CSS_POSITION_ABSOLUTE;
        else if (str_ieqn(v, "fixed", 5)) style->position = CSS_POSITION_FIXED;
        else style->position = CSS_POSITION_STATIC;
    }
    // Width
    else if (str_eqn(prop_lower, "width", 5)) {
        style->props_set |= CSS_PROP_WIDTH;
        style->width = parse_length(&v, v_end);
    }
    // Height
    else if (str_eqn(prop_lower, "height", 6)) {
        style->props_set |= CSS_PROP_HEIGHT;
        style->height = parse_length(&v, v_end);
    }
    // Margin (shorthand and individual)
    else if (str_eqn(prop_lower, "margin-top", 10)) {
        style->props_set |= CSS_PROP_MARGIN;
        style->margin_top = parse_length(&v, v_end);
    }
    else if (str_eqn(prop_lower, "margin-right", 12)) {
        style->props_set |= CSS_PROP_MARGIN;
        style->margin_right = parse_length(&v, v_end);
    }
    else if (str_eqn(prop_lower, "margin-bottom", 13)) {
        style->props_set |= CSS_PROP_MARGIN;
        style->margin_bottom = parse_length(&v, v_end);
    }
    else if (str_eqn(prop_lower, "margin-left", 11)) {
        style->props_set |= CSS_PROP_MARGIN;
        style->margin_left = parse_length(&v, v_end);
    }
    else if (str_eqn(prop_lower, "margin", 6)) {
        style->props_set |= CSS_PROP_MARGIN;
        // Parse 1-4 values
        css_length_t vals[4];
        int num_vals = 0;
        while (v < v_end && num_vals < 4) {
            skip_whitespace(&v, v_end);
            if (v >= v_end) break;
            vals[num_vals++] = parse_length(&v, v_end);
        }
        if (num_vals == 1) {
            style->margin_top = style->margin_right = style->margin_bottom = style->margin_left = vals[0];
        } else if (num_vals == 2) {
            style->margin_top = style->margin_bottom = vals[0];
            style->margin_left = style->margin_right = vals[1];
        } else if (num_vals == 3) {
            style->margin_top = vals[0];
            style->margin_left = style->margin_right = vals[1];
            style->margin_bottom = vals[2];
        } else if (num_vals == 4) {
            style->margin_top = vals[0];
            style->margin_right = vals[1];
            style->margin_bottom = vals[2];
            style->margin_left = vals[3];
        }
    }
    // Padding (shorthand and individual)
    else if (str_eqn(prop_lower, "padding-top", 11)) {
        style->props_set |= CSS_PROP_PADDING;
        style->padding_top = parse_length(&v, v_end);
    }
    else if (str_eqn(prop_lower, "padding-right", 13)) {
        style->props_set |= CSS_PROP_PADDING;
        style->padding_right = parse_length(&v, v_end);
    }
    else if (str_eqn(prop_lower, "padding-bottom", 14)) {
        style->props_set |= CSS_PROP_PADDING;
        style->padding_bottom = parse_length(&v, v_end);
    }
    else if (str_eqn(prop_lower, "padding-left", 12)) {
        style->props_set |= CSS_PROP_PADDING;
        style->padding_left = parse_length(&v, v_end);
    }
    else if (str_eqn(prop_lower, "padding", 7)) {
        style->props_set |= CSS_PROP_PADDING;
        css_length_t vals[4];
        int num_vals = 0;
        while (v < v_end && num_vals < 4) {
            skip_whitespace(&v, v_end);
            if (v >= v_end) break;
            vals[num_vals++] = parse_length(&v, v_end);
        }
        if (num_vals == 1) {
            style->padding_top = style->padding_right = style->padding_bottom = style->padding_left = vals[0];
        } else if (num_vals == 2) {
            style->padding_top = style->padding_bottom = vals[0];
            style->padding_left = style->padding_right = vals[1];
        } else if (num_vals == 3) {
            style->padding_top = vals[0];
            style->padding_left = style->padding_right = vals[1];
            style->padding_bottom = vals[2];
        } else if (num_vals == 4) {
            style->padding_top = vals[0];
            style->padding_right = vals[1];
            style->padding_bottom = vals[2];
            style->padding_left = vals[3];
        }
    }
    // Color
    else if (str_eqn(prop_lower, "color", 5)) {
        style->props_set |= CSS_PROP_COLOR;
        style->color = parse_color(&v, v_end);
    }
    // Background-color
    else if (str_eqn(prop_lower, "background-color", 16)) {
        style->props_set |= CSS_PROP_BG_COLOR;
        style->background_color = parse_color(&v, v_end);
    }
    else if (str_eqn(prop_lower, "background", 10)) {
        // Simple background - just look for color
        style->props_set |= CSS_PROP_BG_COLOR;
        style->background_color = parse_color(&v, v_end);
    }
    // Font-size
    else if (str_eqn(prop_lower, "font-size", 9)) {
        style->props_set |= CSS_PROP_FONT_SIZE;
        // Handle keywords
        if (str_ieqn(v, "small", 5)) {
            style->font_size.value = 12; style->font_size.unit = CSS_UNIT_PX;
        } else if (str_ieqn(v, "medium", 6)) {
            style->font_size.value = 16; style->font_size.unit = CSS_UNIT_PX;
        } else if (str_ieqn(v, "large", 5)) {
            style->font_size.value = 20; style->font_size.unit = CSS_UNIT_PX;
        } else if (str_ieqn(v, "x-large", 7)) {
            style->font_size.value = 24; style->font_size.unit = CSS_UNIT_PX;
        } else if (str_ieqn(v, "xx-large", 8)) {
            style->font_size.value = 32; style->font_size.unit = CSS_UNIT_PX;
        } else if (str_ieqn(v, "x-small", 7)) {
            style->font_size.value = 10; style->font_size.unit = CSS_UNIT_PX;
        } else if (str_ieqn(v, "xx-small", 8)) {
            style->font_size.value = 8; style->font_size.unit = CSS_UNIT_PX;
        } else {
            style->font_size = parse_length(&v, v_end);
        }
    }
    // Font-weight
    else if (str_eqn(prop_lower, "font-weight", 11)) {
        style->props_set |= CSS_PROP_FONT_WEIGHT;
        if (str_ieqn(v, "bold", 4) || str_ieqn(v, "700", 3) || str_ieqn(v, "800", 3) || str_ieqn(v, "900", 3)) {
            style->font_weight = CSS_FONT_WEIGHT_BOLD;
        } else {
            style->font_weight = CSS_FONT_WEIGHT_NORMAL;
        }
    }
    // Font-style
    else if (str_eqn(prop_lower, "font-style", 10)) {
        style->props_set |= CSS_PROP_FONT_STYLE;
        if (str_ieqn(v, "italic", 6) || str_ieqn(v, "oblique", 7)) {
            style->font_style = CSS_FONT_STYLE_ITALIC;
        } else {
            style->font_style = CSS_FONT_STYLE_NORMAL;
        }
    }
    // Text-align
    else if (str_eqn(prop_lower, "text-align", 10)) {
        style->props_set |= CSS_PROP_TEXT_ALIGN;
        if (str_ieqn(v, "center", 6)) style->text_align = CSS_TEXT_ALIGN_CENTER;
        else if (str_ieqn(v, "right", 5)) style->text_align = CSS_TEXT_ALIGN_RIGHT;
        else if (str_ieqn(v, "justify", 7)) style->text_align = CSS_TEXT_ALIGN_JUSTIFY;
        else style->text_align = CSS_TEXT_ALIGN_LEFT;
    }
    // Text-decoration
    else if (str_eqn(prop_lower, "text-decoration", 15)) {
        style->props_set |= CSS_PROP_TEXT_DECORATION;
        if (str_ieqn(v, "underline", 9)) style->text_decoration = CSS_TEXT_DECORATION_UNDERLINE;
        else if (str_ieqn(v, "line-through", 12)) style->text_decoration = CSS_TEXT_DECORATION_LINE_THROUGH;
        else style->text_decoration = CSS_TEXT_DECORATION_NONE;
    }
    // White-space
    else if (str_eqn(prop_lower, "white-space", 11)) {
        style->props_set |= CSS_PROP_WHITE_SPACE;
        if (str_ieqn(v, "pre", 3)) style->white_space = CSS_WHITE_SPACE_PRE;
        else if (str_ieqn(v, "nowrap", 6)) style->white_space = CSS_WHITE_SPACE_NOWRAP;
        else if (str_ieqn(v, "pre-wrap", 8)) style->white_space = CSS_WHITE_SPACE_PRE_WRAP;
        else style->white_space = CSS_WHITE_SPACE_NORMAL;
    }
    // Vertical-align
    else if (str_eqn(prop_lower, "vertical-align", 14)) {
        style->props_set |= CSS_PROP_VERTICAL_ALIGN;
        if (str_ieqn(v, "top", 3)) style->vertical_align = CSS_VERTICAL_ALIGN_TOP;
        else if (str_ieqn(v, "middle", 6)) style->vertical_align = CSS_VERTICAL_ALIGN_MIDDLE;
        else if (str_ieqn(v, "bottom", 6)) style->vertical_align = CSS_VERTICAL_ALIGN_BOTTOM;
        else if (str_ieqn(v, "sub", 3)) style->vertical_align = CSS_VERTICAL_ALIGN_SUB;
        else if (str_ieqn(v, "super", 5)) style->vertical_align = CSS_VERTICAL_ALIGN_SUPER;
        else style->vertical_align = CSS_VERTICAL_ALIGN_BASELINE;
    }
    // Border-width (simplified)
    else if (str_eqn(prop_lower, "border-width", 12) || str_eqn(prop_lower, "border", 6)) {
        style->props_set |= CSS_PROP_BORDER_WIDTH;
        style->border_width = parse_length(&v, v_end);
    }
    // List-style-type
    else if (str_eqn(prop_lower, "list-style-type", 15) || str_eqn(prop_lower, "list-style", 10)) {
        style->props_set |= CSS_PROP_LIST_STYLE;
        if (str_ieqn(v, "none", 4)) style->list_style_type = 0;
        else if (str_ieqn(v, "disc", 4)) style->list_style_type = 1;
        else if (str_ieqn(v, "circle", 6)) style->list_style_type = 2;
        else if (str_ieqn(v, "square", 6)) style->list_style_type = 3;
        else if (str_ieqn(v, "decimal", 7)) style->list_style_type = 4;
    }
}

// Parse inline style attribute (style="...")
static inline void parse_inline_style(const char *style_str, int len, css_style_t *style) {
    const char *p = style_str;
    const char *end = style_str + len;

    while (p < end) {
        skip_whitespace(&p, end);
        if (p >= end) break;

        // Parse property name
        const char *prop_start = p;
        while (p < end && *p != ':' && *p != ';') p++;
        int prop_len = p - prop_start;

        // Trim trailing whitespace from property
        while (prop_len > 0 && (prop_start[prop_len-1] == ' ' || prop_start[prop_len-1] == '\t')) {
            prop_len--;
        }

        if (p >= end || *p != ':') {
            // Skip to next declaration
            while (p < end && *p != ';') p++;
            if (p < end) p++;
            continue;
        }
        p++;  // Skip ':'

        // Parse value
        skip_whitespace(&p, end);
        const char *value_start = p;
        while (p < end && *p != ';' && *p != '!') p++;
        int value_len = p - value_start;

        // Trim trailing whitespace from value
        while (value_len > 0 && (value_start[value_len-1] == ' ' || value_start[value_len-1] == '\t')) {
            value_len--;
        }

        // Skip !important
        if (p < end && *p == '!') {
            while (p < end && *p != ';') p++;
        }

        if (p < end && *p == ';') p++;

        if (prop_len > 0 && value_len > 0) {
            parse_declaration(prop_start, prop_len, value_start, value_len, style);
        }
    }
}

// ============ Selector Parsing ============

// Parse a single selector (may have multiple parts like "div.class#id")
static inline int parse_selector(const char **p, const char *end, css_selector_t *sel) {
    sel->num_parts = 0;

    while (*p < end && sel->num_parts < MAX_SELECTOR_PARTS) {
        skip_whitespace_and_comments(p, end);
        if (*p >= end || **p == '{' || **p == ',') break;

        selector_part_t *part = &sel->parts[sel->num_parts];
        combinator_t *comb = &sel->combinators[sel->num_parts];
        *comb = COMB_NONE;

        // Check for combinator (if not first part)
        if (sel->num_parts > 0) {
            if (**p == '>') {
                *comb = COMB_CHILD;
                (*p)++;
                skip_whitespace_and_comments(p, end);
            } else if (**p == '+') {
                *comb = COMB_ADJACENT;
                (*p)++;
                skip_whitespace_and_comments(p, end);
            } else if (**p == '~') {
                *comb = COMB_SIBLING;
                (*p)++;
                skip_whitespace_and_comments(p, end);
            } else {
                // Space = descendant combinator, but only if there's more selector
                *comb = COMB_DESCENDANT;
            }
        }

        // Parse simple selector
        part->type = SEL_TAG;
        part->name[0] = '\0';
        part->attr_name[0] = '\0';
        part->attr_value[0] = '\0';

        if (**p == '*') {
            part->type = SEL_UNIVERSAL;
            part->name[0] = '*';
            part->name[1] = '\0';
            (*p)++;
        } else if (**p == '.') {
            part->type = SEL_CLASS;
            (*p)++;
            parse_ident(p, end, part->name, 64);
        } else if (**p == '#') {
            part->type = SEL_ID;
            (*p)++;
            parse_ident(p, end, part->name, 64);
        } else if (**p == ':') {
            // Pseudo-class or pseudo-element - skip it
            (*p)++;
            if (*p < end && **p == ':') (*p)++;  // Skip :: for pseudo-elements
            parse_ident(p, end, part->name, 64);  // Skip the name
            // Handle function-like pseudo-classes like :not(), :nth-child()
            if (*p < end && **p == '(') {
                int depth = 1;
                (*p)++;
                while (*p < end && depth > 0) {
                    if (**p == '(') depth++;
                    else if (**p == ')') depth--;
                    (*p)++;
                }
            }
            // Don't add this part - we skip pseudo-selectors
            continue;
        } else if (**p == '[') {
            // Attribute selector
            part->type = SEL_ATTRIBUTE;
            (*p)++;
            parse_ident(p, end, part->attr_name, 32);
            skip_whitespace(p, end);
            if (*p < end && **p == '=') {
                (*p)++;
                skip_whitespace(p, end);
                // Parse value (may be quoted)
                if (*p < end && (**p == '"' || **p == '\'')) {
                    char quote = **p;
                    (*p)++;
                    int i = 0;
                    while (*p < end && **p != quote && i < 63) {
                        part->attr_value[i++] = **p;
                        (*p)++;
                    }
                    part->attr_value[i] = '\0';
                    if (*p < end && **p == quote) (*p)++;
                } else {
                    parse_ident(p, end, part->attr_value, 64);
                }
            }
            if (*p < end && **p == ']') (*p)++;
        } else if ((**p >= 'a' && **p <= 'z') || (**p >= 'A' && **p <= 'Z') || **p == '_') {
            // Tag name
            part->type = SEL_TAG;
            parse_ident(p, end, part->name, 64);
        } else {
            // Unknown character, skip it and break
            (*p)++;
            break;
        }

        sel->num_parts++;

        // Check for chained selectors (no space between, like "div.class")
        if (*p < end && (**p == '.' || **p == '#' || **p == '[')) {
            // Continue parsing chained parts
            sel->combinators[sel->num_parts] = COMB_NONE;  // Same element
        }
    }

    return sel->num_parts > 0 ? 0 : -1;
}

// Calculate specificity: (inline, #id, .class, tag) -> single int
// We use: inline=1000, id=100, class=10, tag=1
static inline int calc_specificity(css_selector_t *sel) {
    int specificity = 0;
    for (int i = 0; i < sel->num_parts; i++) {
        switch (sel->parts[i].type) {
            case SEL_ID: specificity += 100; break;
            case SEL_CLASS: specificity += 10; break;
            case SEL_ATTRIBUTE: specificity += 10; break;
            case SEL_TAG: specificity += 1; break;
            case SEL_UNIVERSAL: break;  // No specificity
        }
    }
    return specificity;
}

// ============ Stylesheet Parsing ============

static inline void free_stylesheet(void) {
    if (!css_kapi) return;
    css_rule_t *r = stylesheet_head;
    while (r) {
        css_rule_t *next = r->next;
        css_kapi->free(r);
        r = next;
    }
    stylesheet_head = stylesheet_tail = NULL;
}

static inline void add_rule(css_selector_t *sel, css_style_t *style) {
    if (!css_kapi) return;

    css_rule_t *rule = css_kapi->malloc(sizeof(css_rule_t));
    if (!rule) return;

    rule->selector = *sel;
    rule->style = *style;
    rule->specificity = calc_specificity(sel);
    rule->next = NULL;

    if (stylesheet_tail) {
        stylesheet_tail->next = rule;
        stylesheet_tail = rule;
    } else {
        stylesheet_head = stylesheet_tail = rule;
    }
}

// Parse a CSS stylesheet (contents of <style> block or external CSS)
static inline void parse_stylesheet(const char *css, int len) {
    const char *p = css;
    const char *end = css + len;

    while (p < end) {
        skip_whitespace_and_comments(&p, end);
        if (p >= end) break;

        // Skip @-rules for now (like @media, @import)
        if (*p == '@') {
            // Find end of at-rule (either ; or matched {})
            int brace_depth = 0;
            while (p < end) {
                if (*p == '{') brace_depth++;
                else if (*p == '}') {
                    brace_depth--;
                    if (brace_depth <= 0) { p++; break; }
                }
                else if (*p == ';' && brace_depth == 0) { p++; break; }
                p++;
            }
            continue;
        }

        // Parse selector(s)
        // Can be comma-separated: "div, p, .class { ... }"
        css_selector_t selectors[16];
        int num_selectors = 0;

        while (p < end && *p != '{' && num_selectors < 16) {
            skip_whitespace_and_comments(&p, end);
            if (p >= end || *p == '{') break;

            const char *before = p;
            if (parse_selector(&p, end, &selectors[num_selectors]) == 0) {
                num_selectors++;
            }
            // If parse_selector didn't advance, skip one character to avoid infinite loop
            if (p == before && p < end) {
                p++;
            }

            skip_whitespace_and_comments(&p, end);
            if (p < end && *p == ',') {
                p++;  // Skip comma, continue to next selector
            }
        }

        if (p >= end || *p != '{') {
            // Skip to next rule - find the next '{' or end
            while (p < end && *p != '{' && *p != '}') p++;
            if (p < end && *p == '{') {
                // Skip this malformed rule's body
                int depth = 1;
                p++;
                while (p < end && depth > 0) {
                    if (*p == '{') depth++;
                    else if (*p == '}') depth--;
                    p++;
                }
            } else if (p < end && *p == '}') {
                p++;
            }
            continue;
        }
        p++;  // Skip '{'

        // Parse declarations
        css_style_t style = {0};

        while (p < end && *p != '}') {
            skip_whitespace_and_comments(&p, end);
            if (p >= end || *p == '}') break;

            // Parse property
            const char *prop_start = p;
            while (p < end && *p != ':' && *p != ';' && *p != '}') p++;
            int prop_len = p - prop_start;

            // Trim
            while (prop_len > 0 && (prop_start[prop_len-1] == ' ' || prop_start[prop_len-1] == '\t')) {
                prop_len--;
            }

            if (p >= end || *p != ':') {
                while (p < end && *p != ';' && *p != '}') p++;
                if (p < end && *p == ';') p++;
                continue;
            }
            p++;  // Skip ':'

            // Parse value
            skip_whitespace(&p, end);
            const char *value_start = p;
            while (p < end && *p != ';' && *p != '}' && *p != '!') p++;
            int value_len = p - value_start;

            // Trim
            while (value_len > 0 && (value_start[value_len-1] == ' ' || value_start[value_len-1] == '\t')) {
                value_len--;
            }

            // Skip !important
            if (p < end && *p == '!') {
                while (p < end && *p != ';' && *p != '}') p++;
            }

            if (p < end && *p == ';') p++;

            if (prop_len > 0 && value_len > 0) {
                parse_declaration(prop_start, prop_len, value_start, value_len, &style);
            }
        }

        if (p < end && *p == '}') p++;

        // Add rules for each selector
        for (int i = 0; i < num_selectors; i++) {
            add_rule(&selectors[i], &style);
        }
    }
}

// ============ Style Computation ============

// Initialize style with defaults
static inline void init_default_style(css_style_t *style) {
    style->props_set = 0;
    style->display = CSS_DISPLAY_INLINE;  // Default for most elements
    style->float_prop = CSS_FLOAT_NONE;
    style->position = CSS_POSITION_STATIC;
    style->visibility = CSS_VISIBILITY_VISIBLE;
    style->width.unit = CSS_UNIT_AUTO;
    style->height.unit = CSS_UNIT_AUTO;
    style->margin_top.unit = CSS_UNIT_NONE;
    style->margin_right.unit = CSS_UNIT_NONE;
    style->margin_bottom.unit = CSS_UNIT_NONE;
    style->margin_left.unit = CSS_UNIT_NONE;
    style->padding_top.unit = CSS_UNIT_NONE;
    style->padding_right.unit = CSS_UNIT_NONE;
    style->padding_bottom.unit = CSS_UNIT_NONE;
    style->padding_left.unit = CSS_UNIT_NONE;
    style->border_width.unit = CSS_UNIT_NONE;
    style->border_color = 0x000000;
    style->color = 0x000000;
    style->background_color = 0xFFFFFF;
    style->font_size.value = 16;
    style->font_size.unit = CSS_UNIT_PX;
    style->font_weight = CSS_FONT_WEIGHT_NORMAL;
    style->font_style = CSS_FONT_STYLE_NORMAL;
    style->text_align = CSS_TEXT_ALIGN_LEFT;
    style->text_decoration = CSS_TEXT_DECORATION_NONE;
    style->white_space = CSS_WHITE_SPACE_NORMAL;
    style->vertical_align = CSS_VERTICAL_ALIGN_BASELINE;
    style->list_style_type = 1;  // disc
}

// Merge style2 into style1 (style2 properties override style1)
static inline void merge_styles(css_style_t *style1, css_style_t *style2) {
    if (style2->props_set & CSS_PROP_DISPLAY) style1->display = style2->display;
    if (style2->props_set & CSS_PROP_FLOAT) style1->float_prop = style2->float_prop;
    if (style2->props_set & CSS_PROP_POSITION) style1->position = style2->position;
    if (style2->props_set & CSS_PROP_VISIBILITY) style1->visibility = style2->visibility;
    if (style2->props_set & CSS_PROP_WIDTH) style1->width = style2->width;
    if (style2->props_set & CSS_PROP_HEIGHT) style1->height = style2->height;
    if (style2->props_set & CSS_PROP_MARGIN) {
        style1->margin_top = style2->margin_top;
        style1->margin_right = style2->margin_right;
        style1->margin_bottom = style2->margin_bottom;
        style1->margin_left = style2->margin_left;
    }
    if (style2->props_set & CSS_PROP_PADDING) {
        style1->padding_top = style2->padding_top;
        style1->padding_right = style2->padding_right;
        style1->padding_bottom = style2->padding_bottom;
        style1->padding_left = style2->padding_left;
    }
    if (style2->props_set & CSS_PROP_COLOR) style1->color = style2->color;
    if (style2->props_set & CSS_PROP_BG_COLOR) style1->background_color = style2->background_color;
    if (style2->props_set & CSS_PROP_FONT_SIZE) style1->font_size = style2->font_size;
    if (style2->props_set & CSS_PROP_FONT_WEIGHT) style1->font_weight = style2->font_weight;
    if (style2->props_set & CSS_PROP_FONT_STYLE) style1->font_style = style2->font_style;
    if (style2->props_set & CSS_PROP_TEXT_ALIGN) style1->text_align = style2->text_align;
    if (style2->props_set & CSS_PROP_TEXT_DECORATION) style1->text_decoration = style2->text_decoration;
    if (style2->props_set & CSS_PROP_WHITE_SPACE) style1->white_space = style2->white_space;
    if (style2->props_set & CSS_PROP_VERTICAL_ALIGN) style1->vertical_align = style2->vertical_align;
    if (style2->props_set & CSS_PROP_BORDER_WIDTH) style1->border_width = style2->border_width;
    if (style2->props_set & CSS_PROP_BORDER_COLOR) style1->border_color = style2->border_color;
    if (style2->props_set & CSS_PROP_LIST_STYLE) style1->list_style_type = style2->list_style_type;

    style1->props_set |= style2->props_set;
}

// Convert length to pixels
static inline int length_to_px(css_length_t *len, int parent_size, int font_size) {
    switch (len->unit) {
        case CSS_UNIT_PX: return (int)len->value;
        case CSS_UNIT_EM: return (int)(len->value * font_size);
        case CSS_UNIT_REM: return (int)(len->value * 16);  // Assume 16px root
        case CSS_UNIT_PERCENT: return (int)(len->value * parent_size / 100);
        case CSS_UNIT_AUTO: return -1;  // Special marker
        default: return 0;
    }
}

#endif /* BROWSER_CSS_H */
