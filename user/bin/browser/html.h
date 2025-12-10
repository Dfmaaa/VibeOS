/*
 * HTML parser for the browser
 */
#ifndef BROWSER_HTML_H
#define BROWSER_HTML_H

#include "str.h"
#include "../../lib/vibe.h"

// Parsed content - simple linked list of text blocks
typedef struct text_block {
    char *text;
    char *link_url;      // URL if this is a link
    int is_heading;      // h1-h6
    int is_bold;
    int is_italic;
    int is_link;
    int is_list_item;    // -1 for unordered, or item number for ordered (1, 2, 3...), 0 for none
    int is_paragraph;
    int is_preformatted; // <pre> or <code>
    int is_blockquote;
    int is_image;        // placeholder for <img>
    int is_newline;      // This block forces a new line before it
    struct text_block *next;
} text_block_t;

// Style state for parsing
typedef struct {
    int heading;      // h1-h6 level (0 = none)
    int bold;
    int italic;
    int link;
    int list_item;
    int preformatted;
    int blockquote;
} style_state_t;

// Global state for HTML parser
static text_block_t *blocks_head = NULL;
static text_block_t *blocks_tail = NULL;
static char current_link_url[512] = "";
static kapi_t *html_kapi = NULL;

static inline void html_set_kapi(kapi_t *k) {
    html_kapi = k;
}

static inline void add_block_styled(const char *text, int len, style_state_t *style, int is_para, int is_image) {
    if (len <= 0) return;
    if (!html_kapi) return;
    kapi_t *k = html_kapi;

    // Skip pure whitespace blocks (unless preformatted)
    if (!style->preformatted) {
        int has_content = 0;
        for (int i = 0; i < len; i++) {
            if (text[i] != ' ' && text[i] != '\t' && text[i] != '\n' && text[i] != '\r') {
                has_content = 1;
                break;
            }
        }
        if (!has_content) return;
    }

    text_block_t *block = k->malloc(sizeof(text_block_t));
    if (!block) return;

    block->text = k->malloc(len + 1);
    if (!block->text) { k->free(block); return; }

    // Copy text - normalize whitespace unless preformatted
    char *dst = block->text;
    if (style->preformatted) {
        // Keep whitespace intact for <pre>/<code>
        for (int i = 0; i < len; i++) {
            *dst++ = text[i];
        }
    } else {
        int last_was_space = 1;
        for (int i = 0; i < len; i++) {
            char c = text[i];
            if (c == '\n' || c == '\r' || c == '\t') c = ' ';
            if (c == ' ' && last_was_space) continue;
            *dst++ = c;
            last_was_space = (c == ' ');
        }
        // Trim trailing space
        if (dst > block->text && *(dst-1) == ' ') dst--;
    }
    *dst = '\0';

    if (dst == block->text && !is_image) {
        k->free(block->text);
        k->free(block);
        return;
    }

    block->is_heading = style->heading;
    block->is_bold = style->bold;
    block->is_italic = style->italic;
    block->is_link = style->link;
    block->is_list_item = style->list_item;
    block->is_paragraph = is_para;
    block->is_preformatted = style->preformatted;
    block->is_blockquote = style->blockquote;
    block->is_image = is_image;
    block->is_newline = 0;  // Default: continue on same line
    block->next = NULL;

    // Store link URL if this is a link
    block->link_url = NULL;
    if (style->link && current_link_url[0]) {
        int url_len = str_len(current_link_url);
        block->link_url = k->malloc(url_len + 1);
        if (block->link_url) {
            str_cpy(block->link_url, current_link_url);
        }
    }

    if (blocks_tail) {
        blocks_tail->next = block;
        blocks_tail = block;
    } else {
        blocks_head = blocks_tail = block;
    }
}

// Backwards-compatible wrapper for simple calls
static inline void add_block(const char *text, int len, int heading, int bold, int link, int list_item, int para) {
    style_state_t style = {0};
    style.heading = heading;
    style.bold = bold;
    style.link = link;
    style.list_item = list_item;
    add_block_styled(text, len, &style, para, 0);
}

// Add a newline marker - next block starts on new line
static inline void add_newline(void) {
    if (!html_kapi) return;
    kapi_t *k = html_kapi;

    // Mark the next block (or create empty marker) to start on new line
    // We do this by creating a minimal block with is_newline set
    text_block_t *block = k->malloc(sizeof(text_block_t));
    if (!block) return;
    block->text = NULL;
    block->link_url = NULL;
    block->is_heading = 0;
    block->is_bold = 0;
    block->is_italic = 0;
    block->is_link = 0;
    block->is_list_item = 0;
    block->is_paragraph = 0;
    block->is_preformatted = 0;
    block->is_blockquote = 0;
    block->is_image = 0;
    block->is_newline = 1;
    block->next = NULL;

    if (blocks_tail) {
        blocks_tail->next = block;
        blocks_tail = block;
    } else {
        blocks_head = blocks_tail = block;
    }
}

static inline void free_blocks(void) {
    if (!html_kapi) return;
    kapi_t *k = html_kapi;

    text_block_t *b = blocks_head;
    while (b) {
        text_block_t *next = b->next;
        if (b->text) k->free(b->text);
        if (b->link_url) k->free(b->link_url);
        k->free(b);
        b = next;
    }
    blocks_head = blocks_tail = NULL;
}

// Helper to extract an attribute value from tag
static inline int extract_attr(const char *attrs_start, const char *tag_end, const char *attr_name, char *out, int max_len) {
    int attr_name_len = str_len(attr_name);
    const char *ap = attrs_start;
    out[0] = '\0';

    while (ap < tag_end) {
        // Skip whitespace
        while (ap < tag_end && (*ap == ' ' || *ap == '\t' || *ap == '\n')) ap++;
        if (ap >= tag_end) break;

        // Check for the attribute
        if (str_ieqn(ap, attr_name, attr_name_len) && (ap[attr_name_len] == '=' || ap[attr_name_len] == ' ')) {
            ap += attr_name_len;
            while (ap < tag_end && *ap == ' ') ap++;
            if (*ap == '=') {
                ap++;
                while (ap < tag_end && *ap == ' ') ap++;
                char quote = 0;
                if (*ap == '"' || *ap == '\'') {
                    quote = *ap++;
                }
                const char *val_start = ap;
                if (quote) {
                    while (ap < tag_end && *ap != quote) ap++;
                } else {
                    while (ap < tag_end && *ap != '>' && *ap != ' ') ap++;
                }
                int val_len = ap - val_start;
                if (val_len > 0 && val_len < max_len) {
                    str_ncpy(out, val_start, val_len);
                    return val_len;
                }
            }
        }
        // Skip to next attribute
        while (ap < tag_end && *ap != ' ' && *ap != '\t') ap++;
    }
    return 0;
}

// Simple HTML parser
static inline void parse_html(const char *html, int len) {
    free_blocks();

    const char *p = html;
    const char *end = html + len;

    int in_head = 0;
    int in_script = 0;
    int in_style = 0;

    // Use style state for all formatting
    style_state_t style = {0};

    // Track list state for ordered lists
    int in_ordered_list = 0;
    int list_item_number = 0;

    const char *text_start = NULL;

    while (p < end) {
        if (*p == '<') {
            // Check for comment first (before any flush)
            if (p + 3 < end && p[1] == '!' && p[2] == '-' && p[3] == '-') {
                // Flush before comment
                if (text_start && !in_head && !in_script && !in_style) {
                    add_block_styled(text_start, p - text_start, &style, 0, 0);
                }
                text_start = NULL;
                // Skip HTML comment
                p += 4;
                while (p + 2 < end && !(p[0] == '-' && p[1] == '-' && p[2] == '>')) p++;
                if (p + 2 < end) p += 3;
                continue;
            }

            // Flush pending text
            if (text_start && !in_head && !in_script && !in_style) {
                add_block_styled(text_start, p - text_start, &style, 0, 0);
            }
            text_start = NULL;

            // Parse tag
            p++;
            int closing = (*p == '/');
            if (closing) p++;

            const char *tag_start = p;
            while (p < end && *p != '>' && *p != ' ' && *p != '\t' && *p != '\n' && *p != '/') p++;
            int tag_len = p - tag_start;

            // Save position after tag name for attribute parsing
            const char *attrs_start = p;

            // Skip to end of tag
            while (p < end && *p != '>') p++;
            const char *tag_end = p;
            if (p < end) p++;

            // Handle tags
            if (str_ieqn(tag_start, "head", 4) && tag_len == 4) {
                in_head = !closing;
            } else if (str_ieqn(tag_start, "script", 6) && tag_len == 6) {
                in_script = !closing;
            } else if (str_ieqn(tag_start, "style", 5) && tag_len == 5) {
                in_style = !closing;
            } else if ((tag_start[0] == 'h' || tag_start[0] == 'H') && tag_len == 2 &&
                       tag_start[1] >= '1' && tag_start[1] <= '6') {
                style.heading = closing ? 0 : (tag_start[1] - '0');
                if (closing) add_newline();
            } else if ((str_ieqn(tag_start, "b", 1) && tag_len == 1) ||
                       (str_ieqn(tag_start, "strong", 6) && tag_len == 6)) {
                style.bold = !closing;
            } else if ((str_ieqn(tag_start, "i", 1) && tag_len == 1) ||
                       (str_ieqn(tag_start, "em", 2) && tag_len == 2)) {
                style.italic = !closing;
            } else if (str_ieqn(tag_start, "a", 1) && tag_len == 1) {
                if (closing) {
                    style.link = 0;
                    current_link_url[0] = '\0';
                } else {
                    style.link = 1;
                    extract_attr(attrs_start, tag_end, "href", current_link_url, 511);
                }
            } else if (str_ieqn(tag_start, "li", 2) && tag_len == 2) {
                if (!closing) {
                    // Add newline before list item, then bullet/number will be rendered by draw code
                    add_newline();
                    if (in_ordered_list) {
                        list_item_number++;
                        style.list_item = list_item_number;  // Store item number (1, 2, 3...)
                    } else {
                        style.list_item = -1;  // -1 means unordered (bullet)
                    }
                } else {
                    style.list_item = 0;
                }
            } else if ((str_ieqn(tag_start, "pre", 3) && tag_len == 3)) {
                if (!closing) {
                    add_newline();
                    style.preformatted = 1;
                } else {
                    style.preformatted = 0;
                    add_newline();
                }
            } else if (str_ieqn(tag_start, "code", 4) && tag_len == 4) {
                // Inline code - just mark as preformatted if not inside <pre>
                if (!style.preformatted) {
                    // We could add special styling later
                }
            } else if (str_ieqn(tag_start, "blockquote", 10) && tag_len == 10) {
                if (!closing) {
                    add_newline();
                    style.blockquote = 1;
                } else {
                    style.blockquote = 0;
                    add_newline();
                }
            } else if (str_ieqn(tag_start, "img", 3) && tag_len == 3) {
                // Image placeholder
                char alt[128] = "";
                extract_attr(attrs_start, tag_end, "alt", alt, 127);

                // Create placeholder text
                char placeholder[160];
                if (alt[0]) {
                    char *d = placeholder;
                    const char *s = "[IMG: ";
                    while (*s) *d++ = *s++;
                    s = alt;
                    while (*s && d < placeholder + 150) *d++ = *s++;
                    *d++ = ']';
                    *d = '\0';
                } else {
                    str_cpy(placeholder, "[IMG]");
                }
                add_block_styled(placeholder, str_len(placeholder), &style, 0, 1);
            } else if ((str_ieqn(tag_start, "p", 1) && tag_len == 1) ||
                       (str_ieqn(tag_start, "div", 3) && tag_len == 3) ||
                       (str_ieqn(tag_start, "br", 2) && (tag_len == 2 || tag_len == 3)) ||
                       (str_ieqn(tag_start, "hr", 2) && tag_len == 2)) {
                add_newline();
                if (str_ieqn(tag_start, "hr", 2)) {
                    add_block("----------------------------------------", 40, 0, 0, 0, 0, 0);
                    add_newline();
                }
            } else if (str_ieqn(tag_start, "ul", 2) && tag_len == 2) {
                add_newline();
                if (!closing) {
                    in_ordered_list = 0;
                    list_item_number = 0;
                }
            } else if (str_ieqn(tag_start, "ol", 2) && tag_len == 2) {
                add_newline();
                if (!closing) {
                    in_ordered_list = 1;
                    list_item_number = 0;
                } else {
                    in_ordered_list = 0;
                }
            } else if (str_ieqn(tag_start, "tr", 2) && tag_len == 2) {
                // Table row - add newline
                if (closing) add_newline();
            } else if (str_ieqn(tag_start, "td", 2) && tag_len == 2) {
                // Table cell - add tab separator
                if (closing) add_block(" | ", 3, 0, 0, 0, 0, 0);
            } else if (str_ieqn(tag_start, "th", 2) && tag_len == 2) {
                // Table header - bold and tab separator
                style.bold = !closing;
                if (closing) add_block(" | ", 3, 0, 0, 0, 0, 0);
            } else if (str_ieqn(tag_start, "table", 5) && tag_len == 5) {
                add_newline();
            } else if (str_ieqn(tag_start, "title", 5) && tag_len == 5) {
                // Skip title text
                in_head = !closing;
            } else if (str_ieqn(tag_start, "span", 4) && tag_len == 4) {
                // Span - ignore, just container
            } else if (str_ieqn(tag_start, "sup", 3) && tag_len == 3) {
                // Superscript - show in brackets
                if (!closing) add_block("^", 1, 0, 0, 0, 0, 0);
            } else if (str_ieqn(tag_start, "sub", 3) && tag_len == 3) {
                // Subscript - show in brackets
                if (!closing) add_block("_", 1, 0, 0, 0, 0, 0);
            }
        } else if (*p == '&') {
            // HTML entity
            if (text_start && !in_head && !in_script && !in_style) {
                add_block_styled(text_start, p - text_start, &style, 0, 0);
            }

            const char *entity_start = p;
            while (p < end && *p != ';' && *p != ' ' && *p != '<') p++;

            // Decode HTML entities
            char decoded[8] = {0};
            int decoded_len = 0;

            if (str_eqn(entity_start, "&amp;", 5)) {
                decoded[0] = '&'; decoded_len = 1;
            } else if (str_eqn(entity_start, "&lt;", 4)) {
                decoded[0] = '<'; decoded_len = 1;
            } else if (str_eqn(entity_start, "&gt;", 4)) {
                decoded[0] = '>'; decoded_len = 1;
            } else if (str_eqn(entity_start, "&quot;", 6)) {
                decoded[0] = '"'; decoded_len = 1;
            } else if (str_eqn(entity_start, "&apos;", 6)) {
                decoded[0] = '\''; decoded_len = 1;
            } else if (str_eqn(entity_start, "&nbsp;", 6)) {
                decoded[0] = ' '; decoded_len = 1;
            } else if (str_eqn(entity_start, "&copy;", 6)) {
                decoded[0] = '('; decoded[1] = 'c'; decoded[2] = ')'; decoded_len = 3;
            } else if (str_eqn(entity_start, "&reg;", 5)) {
                decoded[0] = '('; decoded[1] = 'R'; decoded[2] = ')'; decoded_len = 3;
            } else if (str_eqn(entity_start, "&trade;", 7)) {
                decoded[0] = 'T'; decoded[1] = 'M'; decoded_len = 2;
            } else if (str_eqn(entity_start, "&mdash;", 7) || str_eqn(entity_start, "&#8212;", 7)) {
                decoded[0] = '-'; decoded[1] = '-'; decoded_len = 2;
            } else if (str_eqn(entity_start, "&ndash;", 7) || str_eqn(entity_start, "&#8211;", 7)) {
                decoded[0] = '-'; decoded_len = 1;
            } else if (str_eqn(entity_start, "&bull;", 6) || str_eqn(entity_start, "&#8226;", 7)) {
                decoded[0] = '*'; decoded_len = 1;
            } else if (str_eqn(entity_start, "&hellip;", 8) || str_eqn(entity_start, "&#8230;", 7)) {
                decoded[0] = '.'; decoded[1] = '.'; decoded[2] = '.'; decoded_len = 3;
            } else if (str_eqn(entity_start, "&laquo;", 7)) {
                decoded[0] = '<'; decoded[1] = '<'; decoded_len = 2;
            } else if (str_eqn(entity_start, "&raquo;", 7)) {
                decoded[0] = '>'; decoded[1] = '>'; decoded_len = 2;
            } else if (str_eqn(entity_start, "&ldquo;", 7) || str_eqn(entity_start, "&rdquo;", 7) ||
                       str_eqn(entity_start, "&#8220;", 7) || str_eqn(entity_start, "&#8221;", 7)) {
                decoded[0] = '"'; decoded_len = 1;
            } else if (str_eqn(entity_start, "&lsquo;", 7) || str_eqn(entity_start, "&rsquo;", 7) ||
                       str_eqn(entity_start, "&#8216;", 7) || str_eqn(entity_start, "&#8217;", 7)) {
                decoded[0] = '\''; decoded_len = 1;
            } else if (str_eqn(entity_start, "&pound;", 7)) {
                decoded[0] = 'L'; decoded_len = 1;  // GBP -> L
            } else if (str_eqn(entity_start, "&euro;", 6)) {
                decoded[0] = 'E'; decoded_len = 1;  // EUR -> E
            } else if (str_eqn(entity_start, "&yen;", 5)) {
                decoded[0] = 'Y'; decoded_len = 1;  // JPY -> Y
            } else if (str_eqn(entity_start, "&cent;", 6)) {
                decoded[0] = 'c'; decoded_len = 1;
            } else if (str_eqn(entity_start, "&deg;", 5)) {
                decoded[0] = 'o'; decoded_len = 1;  // deg -> o
            } else if (str_eqn(entity_start, "&plusmn;", 8)) {
                decoded[0] = '+'; decoded[1] = '/'; decoded[2] = '-'; decoded_len = 3;
            } else if (str_eqn(entity_start, "&times;", 7)) {
                decoded[0] = 'x'; decoded_len = 1;
            } else if (str_eqn(entity_start, "&divide;", 8)) {
                decoded[0] = '/'; decoded_len = 1;
            } else if (entity_start[1] == '#') {
                // Numeric entity &#123; or &#x1F;
                int val = 0;
                const char *np = entity_start + 2;
                if (*np == 'x' || *np == 'X') {
                    // Hex
                    np++;
                    while (*np != ';' && np < p) {
                        char c = *np++;
                        if (c >= '0' && c <= '9') val = val * 16 + (c - '0');
                        else if (c >= 'a' && c <= 'f') val = val * 16 + (c - 'a' + 10);
                        else if (c >= 'A' && c <= 'F') val = val * 16 + (c - 'A' + 10);
                    }
                } else {
                    // Decimal
                    while (*np != ';' && np < p) {
                        if (*np >= '0' && *np <= '9') val = val * 10 + (*np - '0');
                        np++;
                    }
                }
                // Convert to ASCII if possible, otherwise show placeholder
                if (val >= 32 && val < 127) {
                    decoded[0] = (char)val; decoded_len = 1;
                } else if (val == 160) {  // non-breaking space
                    decoded[0] = ' '; decoded_len = 1;
                } else if (val == 8212) {  // em-dash
                    decoded[0] = '-'; decoded[1] = '-'; decoded_len = 2;
                } else if (val == 8211) {  // en-dash
                    decoded[0] = '-'; decoded_len = 1;
                } else if (val == 8217 || val == 8216) {  // smart quotes
                    decoded[0] = '\''; decoded_len = 1;
                } else if (val == 8221 || val == 8220) {
                    decoded[0] = '"'; decoded_len = 1;
                }
                // else: skip unknown unicode
            }

            if (decoded_len > 0) {
                add_block_styled(decoded, decoded_len, &style, 0, 0);
            }

            if (*p == ';') p++;
            text_start = p;
        } else {
            if (!text_start) text_start = p;
            p++;
        }
    }

    // Flush remaining text
    if (text_start && !in_head && !in_script && !in_style) {
        add_block_styled(text_start, p - text_start, &style, 0, 0);
    }
}

// Accessor for blocks_head
static inline text_block_t *get_blocks_head(void) {
    return blocks_head;
}

#endif /* BROWSER_HTML_H */
