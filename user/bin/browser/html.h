/*
 * HTML parser for the browser
 *
 * Now builds a proper DOM tree for CSS support.
 */
#ifndef BROWSER_HTML_H
#define BROWSER_HTML_H

#include "str.h"
#include "dom.h"
#include "css.h"
#include "../../lib/vibe.h"

static kapi_t *html_kapi = NULL;

static inline void html_set_kapi(kapi_t *k) {
    html_kapi = k;
    dom_set_kapi(k);
    css_set_kapi(k);
}

// ============ Entity Decoding ============

// Decode a single HTML entity, returns decoded length
static inline int decode_entity(const char *p, const char *end, char *out) {
    // Named entities
    struct { const char *name; int len; const char *decoded; int dec_len; } entities[] = {
        {"&amp;", 5, "&", 1},
        {"&lt;", 4, "<", 1},
        {"&gt;", 4, ">", 1},
        {"&quot;", 6, "\"", 1},
        {"&apos;", 6, "'", 1},
        {"&nbsp;", 6, " ", 1},
        {"&copy;", 6, "(c)", 3},
        {"&reg;", 5, "(R)", 3},
        {"&trade;", 7, "TM", 2},
        {"&mdash;", 7, "--", 2},
        {"&ndash;", 7, "-", 1},
        {"&bull;", 6, "*", 1},
        {"&hellip;", 8, "...", 3},
        {"&laquo;", 7, "<<", 2},
        {"&raquo;", 7, ">>", 2},
        {"&ldquo;", 7, "\"", 1},
        {"&rdquo;", 7, "\"", 1},
        {"&lsquo;", 7, "'", 1},
        {"&rsquo;", 7, "'", 1},
        {"&pound;", 7, "L", 1},
        {"&euro;", 6, "E", 1},
        {"&yen;", 5, "Y", 1},
        {"&cent;", 6, "c", 1},
        {"&deg;", 5, "o", 1},
        {"&plusmn;", 8, "+/-", 3},
        {"&times;", 7, "x", 1},
        {"&divide;", 8, "/", 1},
        {"&larr;", 6, "<-", 2},
        {"&rarr;", 6, "->", 2},
        {"&uarr;", 6, "^", 1},
        {"&darr;", 6, "v", 1},
        {"&middot;", 8, ".", 1},
        {"&sect;", 6, "S", 1},
        {"&para;", 6, "P", 1},
        {"&dagger;", 8, "+", 1},
        {"&Dagger;", 8, "++", 2},
        {"&permil;", 8, "o/oo", 4},
        {"&prime;", 7, "'", 1},
        {"&Prime;", 7, "\"", 1},
        {"&infin;", 7, "inf", 3},
        {"&ne;", 4, "!=", 2},
        {"&le;", 4, "<=", 2},
        {"&ge;", 4, ">=", 2},
        {"&asymp;", 7, "~=", 2},
        {"&equiv;", 7, "===", 3},
        {"&alpha;", 7, "a", 1},
        {"&beta;", 6, "b", 1},
        {"&gamma;", 7, "g", 1},
        {"&delta;", 7, "d", 1},
        {"&epsilon;", 9, "e", 1},
        {"&pi;", 4, "pi", 2},
        {"&sigma;", 7, "s", 1},
        {"&omega;", 7, "w", 1},
        {NULL, 0, NULL, 0}
    };

    int avail = end - p;
    for (int i = 0; entities[i].name; i++) {
        if (avail >= entities[i].len && str_eqn(p, entities[i].name, entities[i].len)) {
            for (int j = 0; j < entities[i].dec_len; j++) {
                out[j] = entities[i].decoded[j];
            }
            return entities[i].dec_len | (entities[i].len << 8);  // Pack both values
        }
    }

    // Numeric entity &#123; or &#x1F;
    if (avail >= 3 && p[0] == '&' && p[1] == '#') {
        int val = 0;
        const char *np = p + 2;
        if (*np == 'x' || *np == 'X') {
            np++;
            while (np < end && *np != ';') {
                char c = *np++;
                if (c >= '0' && c <= '9') val = val * 16 + (c - '0');
                else if (c >= 'a' && c <= 'f') val = val * 16 + (c - 'a' + 10);
                else if (c >= 'A' && c <= 'F') val = val * 16 + (c - 'A' + 10);
                else break;
            }
        } else {
            while (np < end && *np != ';') {
                if (*np >= '0' && *np <= '9') val = val * 10 + (*np - '0');
                else break;
                np++;
            }
        }
        if (np < end && *np == ';') np++;
        int consumed = np - p;

        // Convert to character
        if (val >= 32 && val < 127) {
            out[0] = (char)val;
            return 1 | (consumed << 8);
        } else if (val == 160 || val == 8194 || val == 8195) {  // Various spaces
            out[0] = ' ';
            return 1 | (consumed << 8);
        } else if (val == 8212) {  // em-dash
            out[0] = '-'; out[1] = '-';
            return 2 | (consumed << 8);
        } else if (val == 8211) {  // en-dash
            out[0] = '-';
            return 1 | (consumed << 8);
        } else if (val == 8216 || val == 8217) {  // smart quotes
            out[0] = '\'';
            return 1 | (consumed << 8);
        } else if (val == 8220 || val == 8221) {
            out[0] = '"';
            return 1 | (consumed << 8);
        } else if (val == 8226) {  // bullet
            out[0] = '*';
            return 1 | (consumed << 8);
        } else if (val == 8230) {  // ellipsis
            out[0] = '.'; out[1] = '.'; out[2] = '.';
            return 3 | (consumed << 8);
        }
        // Unknown - skip
        return 0 | (consumed << 8);
    }

    // Not an entity
    return 0;
}

// ============ Attribute Extraction ============

static inline int extract_attr(const char *attrs_start, const char *tag_end,
                               const char *attr_name, char *out, int max_len) {
    int attr_name_len = str_len(attr_name);
    const char *ap = attrs_start;
    out[0] = '\0';

    while (ap < tag_end) {
        // Skip whitespace
        while (ap < tag_end && (*ap == ' ' || *ap == '\t' || *ap == '\n' || *ap == '\r')) ap++;
        if (ap >= tag_end) break;

        // Get attribute name
        const char *name_start = ap;
        while (ap < tag_end && *ap != '=' && *ap != ' ' && *ap != '>') ap++;
        int name_len = ap - name_start;

        // Check if this is the attribute we want
        int match = (name_len == attr_name_len);
        if (match) {
            for (int i = 0; i < name_len; i++) {
                char c1 = name_start[i];
                char c2 = attr_name[i];
                if (c1 >= 'A' && c1 <= 'Z') c1 += 32;
                if (c2 >= 'A' && c2 <= 'Z') c2 += 32;
                if (c1 != c2) { match = 0; break; }
            }
        }

        // Skip whitespace
        while (ap < tag_end && (*ap == ' ' || *ap == '\t')) ap++;

        if (ap < tag_end && *ap == '=') {
            ap++;
            while (ap < tag_end && (*ap == ' ' || *ap == '\t')) ap++;

            // Parse value
            char quote = 0;
            if (ap < tag_end && (*ap == '"' || *ap == '\'')) {
                quote = *ap++;
            }

            const char *val_start = ap;
            if (quote) {
                while (ap < tag_end && *ap != quote) ap++;
            } else {
                while (ap < tag_end && *ap != '>' && *ap != ' ' && *ap != '\t') ap++;
            }
            int val_len = ap - val_start;

            if (match && val_len > 0 && val_len < max_len) {
                str_ncpy(out, val_start, val_len);
                return val_len;
            }

            if (quote && ap < tag_end && *ap == quote) ap++;
        } else if (match) {
            // Boolean attribute
            out[0] = '\0';
            return 0;
        }
    }
    return -1;  // Not found
}

// ============ Self-closing tag check ============

static inline int is_void_element(const char *tag, int tag_len) {
    const char *voids[] = {
        "area", "base", "br", "col", "embed", "hr", "img", "input",
        "link", "meta", "param", "source", "track", "wbr", NULL
    };
    for (int i = 0; voids[i]; i++) {
        if (str_ieqn(tag, voids[i], tag_len) && voids[i][tag_len] == '\0') {
            return 1;
        }
    }
    return 0;
}

// ============ Main HTML Parser ============

// Parse HTML into DOM tree and extract stylesheets
static inline dom_node_t *parse_html(const char *html, int len) {
    if (!html_kapi) return NULL;
    kapi_t *k = html_kapi;

    // Free old stylesheet
    free_stylesheet();

    // Free old DOM
    if (dom_root) {
        free_dom_tree(dom_root);
        dom_root = NULL;
    }

    // Create root element
    dom_node_t *root = create_element("html");
    if (!root) return NULL;

    // Parser state
    dom_node_t *current = root;
    const char *p = html;
    const char *end = html + len;

    // Text accumulator
    char text_buf[4096];
    int text_len = 0;

    // Flush accumulated text
    #define FLUSH_TEXT() do { \
        if (text_len > 0) { \
            /* Normalize whitespace */ \
            char normalized[4096]; \
            int norm_len = 0; \
            int last_was_space = 1; \
            int in_pre = 0; \
            /* Check if current element is <pre> or similar */ \
            dom_node_t *check = current; \
            while (check) { \
                if (str_eqn(check->tag, "pre", 3) || str_eqn(check->tag, "code", 4) || \
                    str_eqn(check->tag, "textarea", 8)) { \
                    in_pre = 1; break; \
                } \
                check = check->parent; \
            } \
            if (in_pre) { \
                for (int i = 0; i < text_len && norm_len < 4095; i++) { \
                    normalized[norm_len++] = text_buf[i]; \
                } \
            } else { \
                for (int i = 0; i < text_len && norm_len < 4095; i++) { \
                    char c = text_buf[i]; \
                    if (c == '\n' || c == '\r' || c == '\t') c = ' '; \
                    if (c == ' ' && last_was_space) continue; \
                    normalized[norm_len++] = c; \
                    last_was_space = (c == ' '); \
                } \
            } \
            if (norm_len > 0 && !(norm_len == 1 && normalized[0] == ' ')) { \
                dom_node_t *txt = create_text(normalized, norm_len); \
                if (txt) append_child(current, txt); \
            } \
            text_len = 0; \
        } \
    } while(0)

    while (p < end) {
        if (*p == '<') {
            // Check for comment
            if (p + 3 < end && p[1] == '!' && p[2] == '-' && p[3] == '-') {
                FLUSH_TEXT();
                p += 4;
                while (p + 2 < end && !(p[0] == '-' && p[1] == '-' && p[2] == '>')) p++;
                if (p + 2 < end) p += 3;
                continue;
            }

            // Check for DOCTYPE
            if (p + 8 < end && str_ieqn(p + 1, "!doctype", 8)) {
                p++;
                while (p < end && *p != '>') p++;
                if (p < end) p++;
                continue;
            }

            // Check for CDATA
            if (p + 8 < end && str_eqn(p + 1, "![CDATA[", 8)) {
                p += 9;
                while (p + 2 < end && !(p[0] == ']' && p[1] == ']' && p[2] == '>')) {
                    if (text_len < 4095) text_buf[text_len++] = *p;
                    p++;
                }
                if (p + 2 < end) p += 3;
                continue;
            }

            FLUSH_TEXT();

            // Parse tag
            p++;
            int closing = (*p == '/');
            if (closing) p++;

            const char *tag_start = p;
            while (p < end && *p != '>' && *p != ' ' && *p != '\t' && *p != '\n' && *p != '/' && *p != '\r') p++;
            int tag_len = p - tag_start;

            if (tag_len == 0) {
                // Malformed tag, skip
                while (p < end && *p != '>') p++;
                if (p < end) p++;
                continue;
            }

            // Save attribute start position
            const char *attrs_start = p;

            // Find end of tag
            while (p < end && *p != '>') p++;
            const char *tag_end = p;

            // Check for self-closing />
            int self_closing = (tag_end > attrs_start && *(tag_end - 1) == '/');
            if (self_closing) tag_end--;

            if (p < end) p++;  // Skip '>'

            // Handle <style> specially
            if (!closing && str_ieqn(tag_start, "style", 5) && tag_len == 5) {
                // Find </style>
                const char *style_start = p;
                while (p + 7 < end && !str_ieqn(p, "</style", 7)) p++;
                int style_len = p - style_start;
                if (style_len > 0) {
                    parse_stylesheet(style_start, style_len);
                }
                // Skip past </style>
                while (p < end && *p != '>') p++;
                if (p < end) p++;
                continue;
            }

            // Handle <script> - skip content
            if (!closing && str_ieqn(tag_start, "script", 6) && tag_len == 6) {
                while (p + 8 < end && !str_ieqn(p, "</script", 8)) p++;
                while (p < end && *p != '>') p++;
                if (p < end) p++;
                continue;
            }

            if (closing) {
                // Closing tag - find matching open tag and pop
                dom_node_t *node = current;
                while (node && node != root) {
                    if (str_ieqn(node->tag, tag_start, tag_len) && node->tag[tag_len] == '\0') {
                        current = node->parent ? node->parent : root;
                        break;
                    }
                    node = node->parent;
                }
            } else {
                // Opening tag - create element
                char tag_name[32];
                int copy_len = tag_len < 31 ? tag_len : 31;
                for (int i = 0; i < copy_len; i++) {
                    char c = tag_start[i];
                    tag_name[i] = (c >= 'A' && c <= 'Z') ? c + 32 : c;
                }
                tag_name[copy_len] = '\0';

                dom_node_t *elem = create_element(tag_name);
                if (!elem) continue;

                // Extract attributes
                char attr_buf[512];

                // id attribute
                if (extract_attr(attrs_start, tag_end, "id", attr_buf, 512) >= 0) {
                    set_attribute(elem, "id", attr_buf, str_len(attr_buf));
                }

                // class attribute
                if (extract_attr(attrs_start, tag_end, "class", attr_buf, 512) >= 0) {
                    set_attribute(elem, "class", attr_buf, str_len(attr_buf));
                }

                // style attribute
                if (extract_attr(attrs_start, tag_end, "style", attr_buf, 512) >= 0) {
                    set_attribute(elem, "style", attr_buf, str_len(attr_buf));
                }

                // href for links
                if (str_eqn(tag_name, "a", 1) && tag_name[1] == '\0') {
                    if (extract_attr(attrs_start, tag_end, "href", attr_buf, 512) >= 0) {
                        // Store href in the node
                        int href_len = str_len(attr_buf);
                        if (href_len > 511) href_len = 511;
                        for (int j = 0; j < href_len; j++) {
                            elem->href[j] = attr_buf[j];
                        }
                        elem->href[href_len] = '\0';
                    }
                }

                // alt for images
                if (str_eqn(tag_name, "img", 3)) {
                    if (extract_attr(attrs_start, tag_end, "alt", attr_buf, 512) >= 0) {
                        // Create placeholder text
                        char placeholder[160];
                        char *d = placeholder;
                        const char *s = "[IMG: ";
                        while (*s) *d++ = *s++;
                        s = attr_buf;
                        while (*s && d < placeholder + 150) *d++ = *s++;
                        *d++ = ']';
                        *d = '\0';

                        append_child(current, elem);
                        dom_node_t *txt = create_text(placeholder, str_len(placeholder));
                        if (txt) append_child(elem, txt);

                        if (is_void_element(tag_name, str_len(tag_name)) || self_closing) {
                            // Don't descend
                        } else {
                            current = elem;
                        }
                        continue;
                    }
                }

                append_child(current, elem);

                // Descend into element (unless void/self-closing)
                if (!is_void_element(tag_name, str_len(tag_name)) && !self_closing) {
                    current = elem;
                }
            }
        } else if (*p == '&') {
            // HTML entity
            char decoded[8];
            int result = decode_entity(p, end, decoded);
            int decoded_len = result & 0xFF;
            int consumed = result >> 8;

            if (consumed > 0) {
                for (int i = 0; i < decoded_len && text_len < 4095; i++) {
                    text_buf[text_len++] = decoded[i];
                }
                p += consumed;
            } else {
                // Not a valid entity, treat as text
                if (text_len < 4095) text_buf[text_len++] = *p;
                p++;
            }
        } else {
            // Regular text
            if (text_len < 4095) {
                text_buf[text_len++] = *p;
            }
            p++;
        }
    }

    FLUSH_TEXT();
    #undef FLUSH_TEXT

    // Set as DOM root
    set_dom_root(root);

    // Compute styles
    compute_styles(root);

    return root;
}

// ============ Legacy API (for backwards compatibility) ============

// Old text_block_t structure (still used by renderer for now)
typedef struct text_block {
    char *text;
    char *link_url;
    int is_heading;
    int is_bold;
    int is_italic;
    int is_link;
    int is_list_item;
    int is_paragraph;
    int is_preformatted;
    int is_blockquote;
    int is_image;
    int is_newline;
    uint32_t color;        // Text color from CSS
    uint32_t bg_color;     // Background color from CSS
    int font_size;         // Font size in pixels
    int margin_left;       // Left margin in pixels
    int is_hidden;         // display:none or visibility:hidden
    struct text_block *next;
} text_block_t;

static text_block_t *blocks_head = NULL;
static text_block_t *blocks_tail = NULL;

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

static inline void add_text_block(const char *text, int len, dom_node_t *node, const char *link_url) {
    if (!html_kapi || len <= 0) return;
    kapi_t *k = html_kapi;

    text_block_t *block = k->malloc(sizeof(text_block_t));
    if (!block) return;

    block->text = k->malloc(len + 1);
    if (!block->text) { k->free(block); return; }
    for (int i = 0; i < len; i++) block->text[i] = text[i];
    block->text[len] = '\0';

    // Copy style from DOM node
    css_style_t *style = &node->style;

    block->is_heading = 0;
    block->is_bold = (style->font_weight == CSS_FONT_WEIGHT_BOLD);
    block->is_italic = (style->font_style == CSS_FONT_STYLE_ITALIC);
    block->is_link = (link_url && link_url[0]);
    block->is_list_item = 0;
    block->is_paragraph = 0;
    block->is_preformatted = (style->white_space == CSS_WHITE_SPACE_PRE ||
                              style->white_space == CSS_WHITE_SPACE_PRE_WRAP);
    block->is_blockquote = 0;
    block->is_image = 0;
    block->is_newline = 0;

    // CSS properties
    block->color = style->color;
    block->bg_color = style->background_color;
    block->font_size = length_to_px(&style->font_size, 16, 16);
    block->margin_left = length_to_px(&style->margin_left, 0, 16);
    if (block->margin_left < 0) block->margin_left = 0;
    block->is_hidden = node->is_hidden;

    // Link URL
    block->link_url = NULL;
    if (link_url && link_url[0]) {
        int url_len = str_len(link_url);
        block->link_url = k->malloc(url_len + 1);
        if (block->link_url) {
            str_cpy(block->link_url, link_url);
        }
    }

    block->next = NULL;

    if (blocks_tail) {
        blocks_tail->next = block;
        blocks_tail = block;
    } else {
        blocks_head = blocks_tail = block;
    }
}

static inline void add_newline_block(dom_node_t *node) {
    if (!html_kapi) return;
    kapi_t *k = html_kapi;

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
    block->color = 0x000000;
    block->bg_color = 0xFFFFFF;
    block->font_size = 16;
    block->margin_left = 0;
    block->is_hidden = node ? node->is_hidden : 0;
    block->next = NULL;

    if (blocks_tail) {
        blocks_tail->next = block;
        blocks_tail = block;
    } else {
        blocks_head = blocks_tail = block;
    }
}

// Track if last block was a newline (to avoid doubling)
static int last_was_newline = 1;

// Add newline only if we didn't just add one
static inline void add_newline_if_needed(dom_node_t *node) {
    if (!last_was_newline) {
        add_newline_block(node);
        last_was_newline = 1;
    }
}

// Convert DOM tree to flat text blocks (for legacy renderer)
static inline void dom_to_blocks_recursive(dom_node_t *node, const char *link_url, int in_list) {
    if (!node) return;

    // Skip hidden nodes
    if (node->is_hidden) return;

    // Track link URL for children
    const char *child_link = link_url;
    if (node->type == DOM_ELEMENT && str_eqn(node->tag, "a", 1) && node->tag[1] == '\0') {
        // Use href from the node if it exists
        if (node->href[0]) {
            child_link = node->href;
        }
    }

    // Handle block-level elements - add newline before (only if needed)
    if (node->type == DOM_ELEMENT && node->is_block) {
        add_newline_if_needed(node);
    }

    // Handle specific elements
    if (node->type == DOM_ELEMENT) {
        // List item bullet/number
        if (str_eqn(node->tag, "li", 2)) {
            // Check if parent is ol or ul
            dom_node_t *parent = node->parent;
            int is_ordered = 0;
            int item_num = 1;
            if (parent && str_eqn(parent->tag, "ol", 2)) {
                is_ordered = 1;
                // Count previous siblings
                dom_node_t *sib = node->prev_sibling;
                while (sib) {
                    if (str_eqn(sib->tag, "li", 2)) item_num++;
                    sib = sib->prev_sibling;
                }
            }

            // Add bullet or number
            char bullet[16];
            if (is_ordered) {
                int i = 0;
                int n = item_num;
                char tmp[8];
                int j = 0;
                do { tmp[j++] = '0' + (n % 10); n /= 10; } while (n > 0);
                while (j > 0) bullet[i++] = tmp[--j];
                bullet[i++] = '.';
                bullet[i++] = ' ';
                bullet[i] = '\0';
            } else {
                bullet[0] = '*'; bullet[1] = ' '; bullet[2] = '\0';
            }
            add_text_block(bullet, str_len(bullet), node, NULL);
            last_was_newline = 0;
        }

        // Horizontal rule
        if (str_eqn(node->tag, "hr", 2)) {
            add_text_block("----------------------------------------", 40, node, NULL);
            last_was_newline = 0;
            add_newline_if_needed(node);
        }

        // <br> - line break
        if (str_eqn(node->tag, "br", 2)) {
            add_newline_block(node);
            last_was_newline = 1;
        }
    }

    // Text node - add to blocks
    if (node->type == DOM_TEXT && node->text && node->text_len > 0) {
        add_text_block(node->text, node->text_len, node->parent ? node->parent : node, link_url);
        last_was_newline = 0;
    }

    // Process children
    dom_node_t *child = node->first_child;
    while (child) {
        int child_in_list = in_list ||
            (node->type == DOM_ELEMENT && (str_eqn(node->tag, "ul", 2) || str_eqn(node->tag, "ol", 2)));
        dom_to_blocks_recursive(child, child_link, child_in_list);
        child = child->next_sibling;
    }

    // Handle block-level elements - add newline after (only if needed)
    if (node->type == DOM_ELEMENT && node->is_block) {
        add_newline_if_needed(node);
    }
}

static inline void dom_to_blocks(void) {
    free_blocks();
    last_was_newline = 1;  // Reset state
    dom_to_blocks_recursive(dom_root, NULL, 0);
}

static inline text_block_t *get_blocks_head(void) {
    return blocks_head;
}

#endif /* BROWSER_HTML_H */
