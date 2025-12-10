/*
 * DOM Tree for VibeOS Browser
 *
 * Proper tree structure needed for CSS selector matching and style inheritance.
 */
#ifndef BROWSER_DOM_H
#define BROWSER_DOM_H

#include "str.h"
#include "css.h"
#include "../../lib/vibe.h"

static kapi_t *dom_kapi = NULL;

static inline void dom_set_kapi(kapi_t *k) {
    dom_kapi = k;
}

// ============ DOM Node Types ============

typedef enum {
    DOM_ELEMENT,
    DOM_TEXT
} dom_node_type_t;

// Forward declaration
typedef struct dom_node dom_node_t;

// DOM Node
struct dom_node {
    dom_node_type_t type;

    // For elements
    char tag[32];             // Tag name (lowercase)
    char id[64];              // id attribute
    char classes[256];        // class attribute (space-separated)
    char inline_style[512];   // style attribute
    char href[512];           // href attribute (for <a> tags)

    // For text nodes
    char *text;
    int text_len;

    // Computed style (after CSS cascade)
    css_style_t style;

    // Tree structure
    dom_node_t *parent;
    dom_node_t *first_child;
    dom_node_t *last_child;
    dom_node_t *next_sibling;
    dom_node_t *prev_sibling;

    // Layout info (computed during layout pass)
    int x, y;           // Position relative to parent
    int width, height;  // Computed dimensions
    int content_width, content_height;  // Inner dimensions

    // Flags
    int is_block;       // Block-level element?
    int is_hidden;      // display:none or visibility:hidden?
};

// Root of DOM tree
static dom_node_t *dom_root = NULL;

// ============ DOM Construction ============

static inline dom_node_t *create_element(const char *tag) {
    if (!dom_kapi) return NULL;

    dom_node_t *node = dom_kapi->malloc(sizeof(dom_node_t));
    if (!node) return NULL;

    node->type = DOM_ELEMENT;
    node->text = NULL;
    node->text_len = 0;
    node->parent = NULL;
    node->first_child = NULL;
    node->last_child = NULL;
    node->next_sibling = NULL;
    node->prev_sibling = NULL;
    node->x = node->y = 0;
    node->width = node->height = 0;
    node->content_width = node->content_height = 0;
    node->is_block = 0;
    node->is_hidden = 0;

    // Copy and lowercase tag
    int i = 0;
    while (tag[i] && i < 31) {
        char c = tag[i];
        node->tag[i] = (c >= 'A' && c <= 'Z') ? c + 32 : c;
        i++;
    }
    node->tag[i] = '\0';

    node->id[0] = '\0';
    node->classes[0] = '\0';
    node->inline_style[0] = '\0';
    node->href[0] = '\0';

    init_default_style(&node->style);

    return node;
}

static inline dom_node_t *create_text(const char *text, int len) {
    if (!dom_kapi) return NULL;

    dom_node_t *node = dom_kapi->malloc(sizeof(dom_node_t));
    if (!node) return NULL;

    node->type = DOM_TEXT;
    node->tag[0] = '\0';
    node->id[0] = '\0';
    node->classes[0] = '\0';
    node->inline_style[0] = '\0';
    node->parent = NULL;
    node->first_child = NULL;
    node->last_child = NULL;
    node->next_sibling = NULL;
    node->prev_sibling = NULL;
    node->x = node->y = 0;
    node->width = node->height = 0;
    node->content_width = node->content_height = 0;
    node->is_block = 0;
    node->is_hidden = 0;

    node->text = dom_kapi->malloc(len + 1);
    if (!node->text) {
        dom_kapi->free(node);
        return NULL;
    }
    for (int i = 0; i < len; i++) {
        node->text[i] = text[i];
    }
    node->text[len] = '\0';
    node->text_len = len;

    init_default_style(&node->style);

    return node;
}

static inline void append_child(dom_node_t *parent, dom_node_t *child) {
    if (!parent || !child) return;

    child->parent = parent;
    child->next_sibling = NULL;
    child->prev_sibling = parent->last_child;

    if (parent->last_child) {
        parent->last_child->next_sibling = child;
    } else {
        parent->first_child = child;
    }
    parent->last_child = child;
}

static inline void free_dom_tree(dom_node_t *node) {
    if (!node || !dom_kapi) return;

    // Free children first
    dom_node_t *child = node->first_child;
    while (child) {
        dom_node_t *next = child->next_sibling;
        free_dom_tree(child);
        child = next;
    }

    // Free text if any
    if (node->text) {
        dom_kapi->free(node->text);
    }

    dom_kapi->free(node);
}

// ============ Attribute Helpers ============

static inline void set_attribute(dom_node_t *node, const char *attr, const char *value, int value_len) {
    if (!node || node->type != DOM_ELEMENT) return;

    // Lowercase attribute name for comparison
    char attr_lower[32];
    int i = 0;
    while (attr[i] && i < 31) {
        char c = attr[i];
        attr_lower[i] = (c >= 'A' && c <= 'Z') ? c + 32 : c;
        i++;
    }
    attr_lower[i] = '\0';

    if (str_eqn(attr_lower, "id", 2)) {
        if (value_len > 63) value_len = 63;
        for (int j = 0; j < value_len; j++) node->id[j] = value[j];
        node->id[value_len] = '\0';
    } else if (str_eqn(attr_lower, "class", 5)) {
        if (value_len > 255) value_len = 255;
        for (int j = 0; j < value_len; j++) node->classes[j] = value[j];
        node->classes[value_len] = '\0';
    } else if (str_eqn(attr_lower, "style", 5)) {
        if (value_len > 511) value_len = 511;
        for (int j = 0; j < value_len; j++) node->inline_style[j] = value[j];
        node->inline_style[value_len] = '\0';
    }
}

// Check if element has a specific class
static inline int has_class(dom_node_t *node, const char *cls, int cls_len) {
    if (!node || node->type != DOM_ELEMENT) return 0;

    const char *p = node->classes;
    while (*p) {
        // Skip whitespace
        while (*p == ' ') p++;
        if (!*p) break;

        // Match class name
        const char *start = p;
        while (*p && *p != ' ') p++;
        int len = p - start;

        if (len == cls_len) {
            int match = 1;
            for (int i = 0; i < len; i++) {
                char c1 = start[i];
                char c2 = cls[i];
                // Case-insensitive comparison
                if (c1 >= 'A' && c1 <= 'Z') c1 += 32;
                if (c2 >= 'A' && c2 <= 'Z') c2 += 32;
                if (c1 != c2) { match = 0; break; }
            }
            if (match) return 1;
        }
    }
    return 0;
}

// ============ Selector Matching ============

// Check if a single selector part matches a node
static inline int selector_part_matches(selector_part_t *part, dom_node_t *node) {
    if (!node || node->type != DOM_ELEMENT) return 0;

    switch (part->type) {
        case SEL_UNIVERSAL:
            return 1;

        case SEL_TAG:
            return str_ieqn(node->tag, part->name, str_len(part->name)) &&
                   node->tag[str_len(part->name)] == '\0';

        case SEL_CLASS:
            return has_class(node, part->name, str_len(part->name));

        case SEL_ID:
            return str_ieqn(node->id, part->name, str_len(part->name)) &&
                   node->id[str_len(part->name)] == '\0';

        case SEL_ATTRIBUTE:
            // For now, just check if attribute exists (class/id only)
            if (str_ieqn(part->attr_name, "class", 5)) {
                return node->classes[0] != '\0';
            } else if (str_ieqn(part->attr_name, "id", 2)) {
                return node->id[0] != '\0';
            }
            return 0;
    }
    return 0;
}

// Check if a full selector matches a node
// This handles combinators: descendant, child, adjacent, sibling
static inline int selector_matches(css_selector_t *sel, dom_node_t *node) {
    if (sel->num_parts == 0) return 0;

    // Start from the last part (the subject of the selector)
    int part_idx = sel->num_parts - 1;
    dom_node_t *current = node;

    while (part_idx >= 0 && current) {
        selector_part_t *part = &sel->parts[part_idx];
        combinator_t comb = sel->combinators[part_idx];

        if (!selector_part_matches(part, current)) {
            // If this is a descendant combinator, try ancestors
            if (part_idx < sel->num_parts - 1 && sel->combinators[part_idx + 1] == COMB_DESCENDANT) {
                current = current->parent;
                continue;  // Don't decrement part_idx
            }
            return 0;
        }

        // Move to previous part
        part_idx--;

        if (part_idx < 0) {
            // All parts matched!
            return 1;
        }

        // Navigate based on combinator of current part
        combinator_t next_comb = sel->combinators[part_idx + 1];
        switch (next_comb) {
            case COMB_NONE:
                // Same element - don't move
                break;
            case COMB_DESCENDANT:
                // Any ancestor
                current = current->parent;
                break;
            case COMB_CHILD:
                // Direct parent only
                current = current->parent;
                break;
            case COMB_ADJACENT:
                // Immediately preceding sibling
                current = current->prev_sibling;
                break;
            case COMB_SIBLING:
                // Any preceding sibling
                current = current->prev_sibling;
                break;
        }
    }

    return part_idx < 0;
}

// ============ Style Computation ============

// Get default display for tag
static inline css_display_t get_default_display(const char *tag) {
    // Block-level elements
    if (str_eqn(tag, "div", 3) || str_eqn(tag, "p", 1) || str_eqn(tag, "h1", 2) ||
        str_eqn(tag, "h2", 2) || str_eqn(tag, "h3", 2) || str_eqn(tag, "h4", 2) ||
        str_eqn(tag, "h5", 2) || str_eqn(tag, "h6", 2) || str_eqn(tag, "ul", 2) ||
        str_eqn(tag, "ol", 2) || str_eqn(tag, "li", 2) || str_eqn(tag, "pre", 3) ||
        str_eqn(tag, "blockquote", 10) || str_eqn(tag, "hr", 2) ||
        str_eqn(tag, "header", 6) || str_eqn(tag, "footer", 6) ||
        str_eqn(tag, "nav", 3) || str_eqn(tag, "article", 7) ||
        str_eqn(tag, "section", 7) || str_eqn(tag, "aside", 5) ||
        str_eqn(tag, "form", 4) || str_eqn(tag, "fieldset", 8) ||
        str_eqn(tag, "figure", 6) || str_eqn(tag, "figcaption", 10) ||
        str_eqn(tag, "address", 7) || str_eqn(tag, "main", 4) ||
        str_eqn(tag, "details", 7) || str_eqn(tag, "summary", 7)) {
        return CSS_DISPLAY_BLOCK;
    }

    // Table elements
    if (str_eqn(tag, "table", 5)) return CSS_DISPLAY_TABLE;
    if (str_eqn(tag, "tr", 2)) return CSS_DISPLAY_TABLE_ROW;
    if (str_eqn(tag, "td", 2) || str_eqn(tag, "th", 2)) return CSS_DISPLAY_TABLE_CELL;

    // List items
    if (str_eqn(tag, "li", 2)) return CSS_DISPLAY_LIST_ITEM;

    // Hidden by default
    if (str_eqn(tag, "head", 4) || str_eqn(tag, "script", 6) ||
        str_eqn(tag, "style", 5) || str_eqn(tag, "meta", 4) ||
        str_eqn(tag, "link", 4) || str_eqn(tag, "title", 5) ||
        str_eqn(tag, "template", 8) || str_eqn(tag, "noscript", 8)) {
        return CSS_DISPLAY_NONE;
    }

    // Default to inline
    return CSS_DISPLAY_INLINE;
}

// Apply user-agent (browser default) styles
static inline void apply_ua_styles(dom_node_t *node) {
    if (node->type != DOM_ELEMENT) return;

    // Set default display
    node->style.display = get_default_display(node->tag);

    // Headings: bold, larger font
    if (node->tag[0] == 'h' && node->tag[1] >= '1' && node->tag[1] <= '6') {
        node->style.font_weight = CSS_FONT_WEIGHT_BOLD;
        int level = node->tag[1] - '0';
        switch (level) {
            case 1: node->style.font_size.value = 32; break;
            case 2: node->style.font_size.value = 24; break;
            case 3: node->style.font_size.value = 20; break;
            case 4: node->style.font_size.value = 18; break;
            case 5: node->style.font_size.value = 16; break;
            case 6: node->style.font_size.value = 14; break;
        }
        node->style.font_size.unit = CSS_UNIT_PX;
        node->style.margin_top.value = 16;
        node->style.margin_top.unit = CSS_UNIT_PX;
        node->style.margin_bottom.value = 16;
        node->style.margin_bottom.unit = CSS_UNIT_PX;
    }

    // Bold elements
    if (str_eqn(node->tag, "b", 1) || str_eqn(node->tag, "strong", 6) ||
        str_eqn(node->tag, "th", 2)) {
        node->style.font_weight = CSS_FONT_WEIGHT_BOLD;
    }

    // Italic elements
    if (str_eqn(node->tag, "i", 1) || str_eqn(node->tag, "em", 2) ||
        str_eqn(node->tag, "cite", 4) || str_eqn(node->tag, "dfn", 3)) {
        node->style.font_style = CSS_FONT_STYLE_ITALIC;
    }

    // Links
    if (str_eqn(node->tag, "a", 1)) {
        node->style.color = 0x0000FF;  // Blue
        node->style.text_decoration = CSS_TEXT_DECORATION_UNDERLINE;
    }

    // Pre/code
    if (str_eqn(node->tag, "pre", 3) || str_eqn(node->tag, "code", 4)) {
        node->style.white_space = CSS_WHITE_SPACE_PRE;
        node->style.background_color = 0xF0F0F0;
    }

    // Paragraphs - margins
    if (str_eqn(node->tag, "p", 1)) {
        node->style.margin_top.value = 16;
        node->style.margin_top.unit = CSS_UNIT_PX;
        node->style.margin_bottom.value = 16;
        node->style.margin_bottom.unit = CSS_UNIT_PX;
    }

    // Lists
    if (str_eqn(node->tag, "ul", 2) || str_eqn(node->tag, "ol", 2)) {
        node->style.margin_left.value = 40;
        node->style.margin_left.unit = CSS_UNIT_PX;
    }

    // Blockquote
    if (str_eqn(node->tag, "blockquote", 10)) {
        node->style.margin_left.value = 40;
        node->style.margin_left.unit = CSS_UNIT_PX;
        node->style.margin_right.value = 40;
        node->style.margin_right.unit = CSS_UNIT_PX;
    }

    // Underline
    if (str_eqn(node->tag, "u", 1)) {
        node->style.text_decoration = CSS_TEXT_DECORATION_UNDERLINE;
    }

    // Strikethrough
    if (str_eqn(node->tag, "s", 1) || str_eqn(node->tag, "strike", 6) ||
        str_eqn(node->tag, "del", 3)) {
        node->style.text_decoration = CSS_TEXT_DECORATION_LINE_THROUGH;
    }

    // Superscript/subscript
    if (str_eqn(node->tag, "sup", 3)) {
        node->style.vertical_align = CSS_VERTICAL_ALIGN_SUPER;
        node->style.font_size.value = 12;
        node->style.font_size.unit = CSS_UNIT_PX;
    }
    if (str_eqn(node->tag, "sub", 3)) {
        node->style.vertical_align = CSS_VERTICAL_ALIGN_SUB;
        node->style.font_size.value = 12;
        node->style.font_size.unit = CSS_UNIT_PX;
    }

    // Small
    if (str_eqn(node->tag, "small", 5)) {
        node->style.font_size.value = 12;
        node->style.font_size.unit = CSS_UNIT_PX;
    }
}

// Compute styles for entire tree
static inline void compute_styles(dom_node_t *node) {
    if (!node) return;

    if (node->type == DOM_ELEMENT) {
        // Start with default style
        init_default_style(&node->style);

        // Apply user-agent styles
        apply_ua_styles(node);

        // Inherit certain properties from parent
        if (node->parent && node->parent->type == DOM_ELEMENT) {
            // Inherited properties: color, font-size, font-weight, font-style, text-align, etc.
            node->style.color = node->parent->style.color;
            node->style.font_size = node->parent->style.font_size;
            node->style.font_weight = node->parent->style.font_weight;
            node->style.font_style = node->parent->style.font_style;
            node->style.text_align = node->parent->style.text_align;
            node->style.white_space = node->parent->style.white_space;
            node->style.visibility = node->parent->style.visibility;
        }

        // Re-apply UA styles (they override inheritance for defaults)
        apply_ua_styles(node);

        // Apply stylesheet rules (sorted by specificity)
        // TODO: Sort rules by specificity. For now, just apply in order.
        css_rule_t *rule = stylesheet_head;
        while (rule) {
            if (selector_matches(&rule->selector, node)) {
                merge_styles(&node->style, &rule->style);
            }
            rule = rule->next;
        }

        // Apply inline styles (highest specificity)
        if (node->inline_style[0]) {
            css_style_t inline_parsed = {0};
            parse_inline_style(node->inline_style, str_len(node->inline_style), &inline_parsed);
            merge_styles(&node->style, &inline_parsed);
        }

        // Update flags
        node->is_block = (node->style.display == CSS_DISPLAY_BLOCK ||
                          node->style.display == CSS_DISPLAY_LIST_ITEM ||
                          node->style.display == CSS_DISPLAY_TABLE ||
                          node->style.display == CSS_DISPLAY_FLEX);
        node->is_hidden = (node->style.display == CSS_DISPLAY_NONE ||
                           node->style.visibility == CSS_VISIBILITY_HIDDEN);
    } else {
        // Text node - inherit from parent
        if (node->parent) {
            node->style = node->parent->style;
        }
    }

    // Recurse to children
    dom_node_t *child = node->first_child;
    while (child) {
        compute_styles(child);
        child = child->next_sibling;
    }
}

// ============ DOM Access Helpers ============

static inline dom_node_t *get_dom_root(void) {
    return dom_root;
}

static inline void set_dom_root(dom_node_t *root) {
    if (dom_root) {
        free_dom_tree(dom_root);
    }
    dom_root = root;
}

// Count all nodes for debugging
static inline int count_nodes(dom_node_t *node) {
    if (!node) return 0;
    int count = 1;
    dom_node_t *child = node->first_child;
    while (child) {
        count += count_nodes(child);
        child = child->next_sibling;
    }
    return count;
}

#endif /* BROWSER_DOM_H */
