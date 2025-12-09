/*
 * VibeOS Web Browser
 *
 * A simple text-mode web browser with basic HTML rendering.
 *
 * Usage: browser [url]
 * Example: browser http://example.com/
 */

#include "../lib/vibe.h"
#include "../lib/gfx.h"

static kapi_t *k;

// ============ String Helpers ============

static int str_len(const char *s) {
    int len = 0;
    while (s[len]) len++;
    return len;
}

static int str_eqn(const char *a, const char *b, int n) {
    while (n > 0 && *a && *b && *a == *b) { a++; b++; n--; }
    return n == 0;
}

static int str_ieqn(const char *a, const char *b, int n) {
    while (n > 0 && *a && *b) {
        char ca = *a, cb = *b;
        if (ca >= 'A' && ca <= 'Z') ca += 32;
        if (cb >= 'A' && cb <= 'Z') cb += 32;
        if (ca != cb) return 0;
        a++; b++; n--;
    }
    return n == 0;
}

static void str_cpy(char *dst, const char *src) {
    while (*src) *dst++ = *src++;
    *dst = '\0';
}

static void str_ncpy(char *dst, const char *src, int n) {
    while (n > 0 && *src) { *dst++ = *src++; n--; }
    *dst = '\0';
}

static int parse_int(const char *s) {
    int n = 0;
    while (*s >= '0' && *s <= '9') {
        n = n * 10 + (*s - '0');
        s++;
    }
    return n;
}

// ============ URL Parsing ============

typedef struct {
    char host[256];
    char path[512];
    int port;
} url_t;

static int parse_url(const char *url, url_t *out) {
    if (str_eqn(url, "http://", 7)) url += 7;

    const char *host_start = url;
    const char *host_end = url;
    while (*host_end && *host_end != '/' && *host_end != ':') host_end++;

    int host_len = host_end - host_start;
    if (host_len >= 256) return -1;
    str_ncpy(out->host, host_start, host_len);

    out->port = 80;
    if (*host_end == ':') {
        host_end++;
        out->port = parse_int(host_end);
        while (*host_end >= '0' && *host_end <= '9') host_end++;
    }

    if (*host_end == '/') str_cpy(out->path, host_end);
    else str_cpy(out->path, "/");

    return 0;
}

// ============ HTTP Client ============

typedef struct {
    int status_code;
    int content_length;
    char location[512];
    int header_len;
} http_response_t;

static int find_header_end(const char *buf, int len) {
    for (int i = 0; i < len - 3; i++) {
        if (buf[i] == '\r' && buf[i+1] == '\n' &&
            buf[i+2] == '\r' && buf[i+3] == '\n') {
            return i + 4;
        }
    }
    return -1;
}

static int parse_headers(const char *buf, int len, http_response_t *resp) {
    memset(resp, 0, sizeof(*resp));
    resp->content_length = -1;
    resp->header_len = find_header_end(buf, len);
    if (resp->header_len < 0) return -1;

    const char *p = buf;
    if (!str_eqn(p, "HTTP/1.", 7)) return -1;
    p += 7;
    while (*p && *p != ' ') p++;
    while (*p == ' ') p++;
    resp->status_code = parse_int(p);

    while (*p && *p != '\r') p++;
    if (*p == '\r') p += 2;

    while (p < buf + resp->header_len - 2) {
        const char *line_end = p;
        while (line_end < buf + resp->header_len && *line_end != '\r') line_end++;

        if (str_ieqn(p, "Content-Length:", 15)) {
            const char *val = p + 15;
            while (*val == ' ') val++;
            resp->content_length = parse_int(val);
        } else if (str_ieqn(p, "Location:", 9)) {
            const char *val = p + 9;
            while (*val == ' ') val++;
            int loc_len = line_end - val;
            if (loc_len >= 512) loc_len = 511;
            str_ncpy(resp->location, val, loc_len);
        }
        p = line_end + 2;
    }
    return 0;
}

static int http_get(const char *host, const char *path, int port,
                    char *response, int max_response, http_response_t *resp) {
    uint32_t ip = k->dns_resolve(host);
    if (ip == 0) return -1;

    int sock = k->tcp_connect(ip, port);
    if (sock < 0) return -1;

    char request[1024];
    char *p = request;
    const char *s;

    s = "GET "; while (*s) *p++ = *s++;
    s = path; while (*s) *p++ = *s++;
    s = " HTTP/1.0\r\nHost: "; while (*s) *p++ = *s++;
    s = host; while (*s) *p++ = *s++;
    s = "\r\nUser-Agent: Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/120.0.0.0 Safari/537.36\r\nAccept: text/html,*/*\r\nConnection: close\r\n\r\n";
    while (*s) *p++ = *s++;
    *p = '\0';

    if (k->tcp_send(sock, request, p - request) < 0) {
        k->tcp_close(sock);
        return -1;
    }

    int total = 0;
    int timeout = 0;
    resp->header_len = 0;

    while (total < max_response - 1 && timeout < 500) {
        int n = k->tcp_recv(sock, response + total, max_response - 1 - total);
        if (n < 0) break;  // Connection closed
        if (n == 0) {
            k->net_poll();
            k->sleep_ms(10);
            timeout++;
            continue;
        }
        total += n;
        timeout = 0;

        // Check if we got headers yet
        if (resp->header_len == 0) {
            response[total] = '\0';
            parse_headers(response, total, resp);

            // If we have Content-Length and got all content, we're done
            if (resp->header_len > 0 && resp->content_length >= 0) {
                int body_received = total - resp->header_len;
                if (body_received >= resp->content_length) break;
            }
        }
    }

    response[total] = '\0';
    k->tcp_close(sock);
    if (resp->header_len == 0) parse_headers(response, total, resp);
    return total;
}

static int is_redirect(int status) {
    return status == 301 || status == 302 || status == 307 || status == 308;
}

// ============ HTML Parser ============

// Parsed content - simple linked list of text blocks
typedef struct text_block {
    char *text;
    char *link_url;      // URL if this is a link
    int is_heading;      // h1-h6
    int is_bold;
    int is_link;
    int is_list_item;
    int is_paragraph;
    struct text_block *next;
} text_block_t;

// Clickable link regions for hit testing
typedef struct {
    int x, y, w, h;      // Bounding box (relative to content, not scroll)
    char url[512];
} link_region_t;

#define MAX_LINK_REGIONS 256
static link_region_t link_regions[MAX_LINK_REGIONS];
static int num_link_regions = 0;

// Current link URL being parsed
static char current_link_url[512] = "";

static text_block_t *blocks_head = NULL;
static text_block_t *blocks_tail = NULL;

static void add_block(const char *text, int len, int heading, int bold, int link, int list_item, int para) {
    if (len <= 0) return;

    // Skip pure whitespace blocks
    int has_content = 0;
    for (int i = 0; i < len; i++) {
        if (text[i] != ' ' && text[i] != '\t' && text[i] != '\n' && text[i] != '\r') {
            has_content = 1;
            break;
        }
    }
    if (!has_content) return;

    text_block_t *block = k->malloc(sizeof(text_block_t));
    if (!block) return;

    block->text = k->malloc(len + 1);
    if (!block->text) { k->free(block); return; }

    // Copy and normalize whitespace
    char *dst = block->text;
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
    *dst = '\0';

    if (dst == block->text) {
        k->free(block->text);
        k->free(block);
        return;
    }

    block->is_heading = heading;
    block->is_bold = bold;
    block->is_link = link;
    block->is_list_item = list_item;
    block->is_paragraph = para;
    block->next = NULL;

    // Store link URL if this is a link
    block->link_url = NULL;
    if (link && current_link_url[0]) {
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

static void free_blocks(void) {
    text_block_t *b = blocks_head;
    while (b) {
        text_block_t *next = b->next;
        if (b->text) k->free(b->text);
        if (b->link_url) k->free(b->link_url);
        k->free(b);
        b = next;
    }
    blocks_head = blocks_tail = NULL;
    num_link_regions = 0;
}

// Simple HTML parser
static void parse_html(const char *html, int len) {
    free_blocks();

    const char *p = html;
    const char *end = html + len;

    int in_head = 0;
    int in_script = 0;
    int in_style = 0;
    int heading = 0;
    int bold = 0;
    int link = 0;
    int list_item = 0;

    const char *text_start = NULL;

    while (p < end) {
        if (*p == '<') {
            // Flush any pending text
            if (text_start && !in_head && !in_script && !in_style) {
                add_block(text_start, p - text_start, heading, bold, link, list_item, 0);
            }
            text_start = NULL;

            // Parse tag
            p++;
            int closing = (*p == '/');
            if (closing) p++;

            const char *tag_start = p;
            while (p < end && *p != '>' && *p != ' ' && *p != '\t' && *p != '\n') p++;
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
            } else if (tag_start[0] == 'h' && tag_len == 2 &&
                       tag_start[1] >= '1' && tag_start[1] <= '6') {
                heading = closing ? 0 : (tag_start[1] - '0');
                if (closing) add_block("\n", 1, 0, 0, 0, 0, 1);
            } else if ((str_ieqn(tag_start, "b", 1) && tag_len == 1) ||
                       (str_ieqn(tag_start, "strong", 6) && tag_len == 6)) {
                bold = !closing;
            } else if (str_ieqn(tag_start, "a", 1) && tag_len == 1) {
                if (closing) {
                    link = 0;
                    current_link_url[0] = '\0';
                } else {
                    link = 1;
                    // Extract href attribute
                    current_link_url[0] = '\0';
                    const char *ap = attrs_start;
                    while (ap < tag_end) {
                        // Skip whitespace
                        while (ap < tag_end && (*ap == ' ' || *ap == '\t' || *ap == '\n')) ap++;
                        if (ap >= tag_end) break;

                        // Check for href
                        if (str_ieqn(ap, "href", 4)) {
                            ap += 4;
                            while (ap < tag_end && *ap == ' ') ap++;
                            if (*ap == '=') {
                                ap++;
                                while (ap < tag_end && *ap == ' ') ap++;
                                char quote = 0;
                                if (*ap == '"' || *ap == '\'') {
                                    quote = *ap++;
                                }
                                const char *href_start = ap;
                                while (ap < tag_end && *ap != quote && *ap != '>' && *ap != ' ') ap++;
                                int href_len = ap - href_start;
                                if (href_len > 0 && href_len < 511) {
                                    str_ncpy(current_link_url, href_start, href_len);
                                }
                                break;
                            }
                        }
                        // Skip to next attribute
                        while (ap < tag_end && *ap != ' ' && *ap != '\t') ap++;
                    }
                }
            } else if (str_ieqn(tag_start, "li", 2) && tag_len == 2) {
                if (!closing) {
                    add_block("• ", 2, 0, 0, 0, 1, 0);
                    list_item = 1;
                } else {
                    list_item = 0;
                    add_block("\n", 1, 0, 0, 0, 0, 0);
                }
            } else if ((str_ieqn(tag_start, "p", 1) && tag_len == 1) ||
                       (str_ieqn(tag_start, "div", 3) && tag_len == 3) ||
                       (str_ieqn(tag_start, "br", 2) && tag_len == 2)) {
                add_block("\n", 1, 0, 0, 0, 0, 1);
            } else if ((str_ieqn(tag_start, "ul", 2) && tag_len == 2) ||
                       (str_ieqn(tag_start, "ol", 2) && tag_len == 2)) {
                add_block("\n", 1, 0, 0, 0, 0, 1);
            } else if (str_ieqn(tag_start, "title", 5) && tag_len == 5) {
                // Will capture title text
            }
        } else if (*p == '&') {
            // HTML entity
            if (text_start && !in_head && !in_script && !in_style) {
                add_block(text_start, p - text_start, heading, bold, link, list_item, 0);
            }

            const char *entity_start = p;
            while (p < end && *p != ';' && *p != ' ' && *p != '<') p++;

            // Decode common entities
            if (str_eqn(entity_start, "&amp;", 5)) {
                add_block("&", 1, heading, bold, link, list_item, 0);
            } else if (str_eqn(entity_start, "&lt;", 4)) {
                add_block("<", 1, heading, bold, link, list_item, 0);
            } else if (str_eqn(entity_start, "&gt;", 4)) {
                add_block(">", 1, heading, bold, link, list_item, 0);
            } else if (str_eqn(entity_start, "&quot;", 6)) {
                add_block("\"", 1, heading, bold, link, list_item, 0);
            } else if (str_eqn(entity_start, "&nbsp;", 6)) {
                add_block(" ", 1, heading, bold, link, list_item, 0);
            } else if (str_eqn(entity_start, "&copy;", 6)) {
                add_block("(c)", 3, heading, bold, link, list_item, 0);
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
        add_block(text_start, p - text_start, heading, bold, link, list_item, 0);
    }
}

// ============ Browser UI ============

#define WIN_WIDTH 600
#define WIN_HEIGHT 400
#define ADDR_BAR_HEIGHT 24
#define CONTENT_Y (ADDR_BAR_HEIGHT + 2)
#define CHAR_W 8
#define CHAR_H 16
#define MARGIN 8

static int window_id = -1;
static uint32_t *win_buf = NULL;
static int win_w, win_h;
static char current_url[512] = "";
static int scroll_offset = 0;
static int content_height = 0;
static int editing_url = 0;
static char url_input[512] = "";
static int cursor_pos = 0;
static int dragging_scrollbar = 0;
static int drag_start_y = 0;
static int drag_start_scroll = 0;

// History for back button
#define MAX_HISTORY 32
static char history[MAX_HISTORY][512];
static int history_pos = -1;
static int history_len = 0;

// Graphics context
static gfx_ctx_t gfx;

// Scrollbar dimensions (calculated in draw)
static int scrollbar_y = 0;
static int scrollbar_h = 0;
#define SCROLLBAR_W 12

static void draw_browser(void) {
    if (!win_buf) return;

    // Clear link regions
    num_link_regions = 0;

    // Clear
    gfx_fill_rect(&gfx, 0, 0, win_w, win_h, COLOR_WHITE);

    // Address bar background
    gfx_fill_rect(&gfx, 0, 0, win_w, ADDR_BAR_HEIGHT, 0x00DDDDDD);
    gfx_draw_rect(&gfx, 0, ADDR_BAR_HEIGHT - 1, win_w, 1, COLOR_BLACK);

    // Back button
    #define BACK_BTN_W 24
    uint32_t back_color = (history_pos > 0) ? COLOR_BLACK : 0x00888888;
    gfx_fill_rect(&gfx, 4, 4, BACK_BTN_W, 16, 0x00EEEEEE);
    gfx_draw_rect(&gfx, 4, 4, BACK_BTN_W, 16, back_color);
    gfx_draw_string(&gfx, 8, 4, "<", back_color, 0x00EEEEEE);

    // URL input box (shifted right for back button)
    int url_x = 4 + BACK_BTN_W + 4;
    gfx_fill_rect(&gfx, url_x, 4, win_w - url_x - 4, 16, COLOR_WHITE);
    gfx_draw_rect(&gfx, url_x, 4, win_w - url_x - 4, 16, COLOR_BLACK);

    // URL text
    const char *display_url = editing_url ? url_input : current_url;
    gfx_draw_string(&gfx, url_x + 4, 4, display_url, COLOR_BLACK, COLOR_WHITE);

    // Cursor when editing
    if (editing_url) {
        int cursor_x = url_x + 4 + cursor_pos * CHAR_W;
        gfx_fill_rect(&gfx, cursor_x, 5, 1, 14, COLOR_BLACK);
    }

    // Content area
    int y = CONTENT_Y + MARGIN - scroll_offset;
    int max_chars = (win_w - MARGIN * 2) / CHAR_W;

    text_block_t *block = blocks_head;
    while (block) {
        if (y > win_h) break;

        const char *text = block->text;
        int len = str_len(text);

        // Word wrap
        int pos = 0;
        while (pos < len) {
            // Find line break point
            int line_len = 0;
            int last_space = -1;

            while (pos + line_len < len && line_len < max_chars) {
                if (text[pos + line_len] == '\n') break;
                if (text[pos + line_len] == ' ') last_space = line_len;
                line_len++;
            }

            // Break at word boundary if possible
            if (pos + line_len < len && last_space > 0 && line_len >= max_chars) {
                line_len = last_space + 1;
            }

            // Draw line if visible
            if (y + CHAR_H > CONTENT_Y && y < win_h) {
                uint32_t fg = COLOR_BLACK;
                if (block->is_link) fg = 0x000000FF;  // Blue for links

                // Draw character by character for styling
                int actual_chars = 0;
                for (int i = 0; i < line_len && text[pos + i] != '\n'; i++) {
                    char c = text[pos + i];
                    int x = MARGIN + i * CHAR_W;
                    if (x + CHAR_W > win_w - MARGIN) break;
                    gfx_draw_char(&gfx, x, y, c, fg, COLOR_WHITE);
                    actual_chars++;
                }

                // Register link region for hit testing
                if (block->is_link && block->link_url && num_link_regions < MAX_LINK_REGIONS && actual_chars > 0) {
                    link_region_t *lr = &link_regions[num_link_regions++];
                    lr->x = MARGIN;
                    lr->y = y;
                    lr->w = actual_chars * CHAR_W;
                    lr->h = CHAR_H;
                    str_ncpy(lr->url, block->link_url, 511);
                }

                // Underline for headings
                if (block->is_heading == 1) {
                    gfx_fill_rect(&gfx, MARGIN, y + CHAR_H - 2,
                                  line_len * CHAR_W, 2, COLOR_BLACK);
                }
            }

            pos += line_len;
            y += CHAR_H;

            // Skip newline
            if (pos < len && text[pos] == '\n') pos++;
        }

        // Extra space after paragraphs
        if (block->is_paragraph || block->is_heading) {
            y += CHAR_H / 2;
        }

        block = block->next;
    }

    content_height = y + scroll_offset - CONTENT_Y;

    // Scrollbar if needed
    if (content_height > win_h - CONTENT_Y) {
        int content_area = win_h - CONTENT_Y - 16;  // minus status bar
        scrollbar_h = content_area * content_area / content_height;
        if (scrollbar_h < 20) scrollbar_h = 20;
        int max_scroll = content_height - content_area;
        if (max_scroll > 0) {
            scrollbar_y = CONTENT_Y + scroll_offset * (content_area - scrollbar_h) / max_scroll;
        } else {
            scrollbar_y = CONTENT_Y;
        }
        // Draw scrollbar track
        gfx_fill_rect(&gfx, win_w - SCROLLBAR_W, CONTENT_Y, SCROLLBAR_W, content_area, 0x00CCCCCC);
        // Draw scrollbar thumb
        gfx_fill_rect(&gfx, win_w - SCROLLBAR_W + 2, scrollbar_y, SCROLLBAR_W - 4, scrollbar_h, 0x00666666);
    } else {
        scrollbar_h = 0;
    }

    // Status bar
    gfx_fill_rect(&gfx, 0, win_h - 16, win_w, 16, 0x00DDDDDD);
    if (blocks_head) {
        gfx_draw_string(&gfx, 4, win_h - 16, "Ready", COLOR_BLACK, 0x00DDDDDD);
    } else if (current_url[0]) {
        gfx_draw_string(&gfx, 4, win_h - 16, "Loading...", COLOR_BLACK, 0x00DDDDDD);
    } else {
        gfx_draw_string(&gfx, 4, win_h - 16, "Enter URL and press Enter", COLOR_BLACK, 0x00DDDDDD);
    }

    k->window_invalidate(window_id);
}

// Resolve a potentially relative URL against the current URL
static void resolve_url(const char *href, char *out, int max_len) {
    if (str_eqn(href, "http://", 7) || str_eqn(href, "https://", 8)) {
        // Absolute URL
        str_ncpy(out, href, max_len - 1);
        return;
    }

    // Parse current URL to get host
    url_t base;
    if (parse_url(current_url, &base) < 0) {
        str_ncpy(out, href, max_len - 1);
        return;
    }

    char *p = out;
    char *end = out + max_len - 1;

    // Build http://host
    const char *s = "http://";
    while (*s && p < end) *p++ = *s++;
    s = base.host;
    while (*s && p < end) *p++ = *s++;

    if (href[0] == '/') {
        // Absolute path
        s = href;
        while (*s && p < end) *p++ = *s++;
    } else {
        // Relative path - append to current directory
        // Find last / in current path
        int last_slash = 0;
        for (int i = 0; base.path[i]; i++) {
            if (base.path[i] == '/') last_slash = i;
        }
        // Copy path up to and including last /
        for (int i = 0; i <= last_slash && p < end; i++) {
            *p++ = base.path[i];
        }
        // Append relative href
        s = href;
        while (*s && p < end) *p++ = *s++;
    }
    *p = '\0';
}

static void navigate_internal(const char *url, int add_to_history);

static void go_back(void) {
    if (history_pos > 0) {
        history_pos--;
        navigate_internal(history[history_pos], 0);
    }
}

static void navigate(const char *url) {
    // Add to history
    if (history_pos < MAX_HISTORY - 1) {
        history_pos++;
        str_ncpy(history[history_pos], url, 511);
        history_len = history_pos + 1;
    }
    navigate_internal(url, 1);
}

static void navigate_internal(const char *url, int add_to_history) {
    (void)add_to_history;
    str_cpy(current_url, url);
    str_cpy(url_input, url);
    free_blocks();
    scroll_offset = 0;
    draw_browser();

    url_t parsed;
    if (parse_url(url, &parsed) < 0) {
        add_block("Error: Invalid URL", 18, 1, 0, 0, 0, 0);
        draw_browser();
        return;
    }

    char *response = k->malloc(131072);  // 128KB
    if (!response) {
        add_block("Error: Out of memory", 20, 1, 0, 0, 0, 0);
        draw_browser();
        return;
    }

    http_response_t resp;
    int redirects = 0;

    while (1) {
        int len = http_get(parsed.host, parsed.path, parsed.port, response, 131072, &resp);

        if (len <= 0) {
            add_block("Error: No response from server", 30, 1, 0, 0, 0, 0);
            break;
        }

        if (is_redirect(resp.status_code) && resp.location[0] && redirects < 5) {
            // Don't follow HTTPS redirects - just render whatever content we got
            if (str_eqn(resp.location, "https://", 8)) {
                // HTTPS redirect - render the response body as-is
                if (resp.header_len > 0 && resp.header_len < len) {
                    parse_html(response + resp.header_len, len - resp.header_len);
                }
                break;
            }
            redirects++;
            if (resp.location[0] == '/') {
                str_cpy(parsed.path, resp.location);
            } else {
                parse_url(resp.location, &parsed);
            }
            continue;
        }

        // For non-200 responses, still try to render the body (many sites return HTML error pages)
        if (resp.header_len > 0 && resp.header_len < len) {
            parse_html(response + resp.header_len, len - resp.header_len);
        }
        break;
    }

    k->free(response);
    draw_browser();
}

int main(kapi_t *kapi, int argc, char **argv) {
    k = kapi;

    if (!k->window_create) {
        k->puts("Browser requires desktop environment\n");
        return 1;
    }

    // Create window
    window_id = k->window_create(50, 50, WIN_WIDTH, WIN_HEIGHT, "VibeOS Browser");
    if (window_id < 0) {
        k->puts("Failed to create window\n");
        return 1;
    }

    win_buf = k->window_get_buffer(window_id, &win_w, &win_h);
    if (!win_buf) {
        k->window_destroy(window_id);
        return 1;
    }

    // Setup graphics context
    gfx_init(&gfx, win_buf, win_w, win_h, k->font_data);

    // Navigate to initial URL if provided
    if (argc > 1) {
        str_cpy(url_input, argv[1]);
        navigate(argv[1]);
    } else {
        str_cpy(url_input, "http://");
        cursor_pos = 7;
        editing_url = 1;
    }

    draw_browser();

    // Event loop
    int running = 1;
    while (running) {
        int event_type, data1, data2, data3;
        while (k->window_poll_event(window_id, &event_type, &data1, &data2, &data3)) {
            switch (event_type) {
                case WIN_EVENT_CLOSE:
                    running = 0;
                    break;

                case WIN_EVENT_KEY: {
                    int key = data1;

                    if (editing_url) {
                        if (key == '\n' || key == '\r') {
                            // Navigate
                            editing_url = 0;
                            navigate(url_input);
                        } else if (key == 27) {  // Escape
                            editing_url = 0;
                            str_cpy(url_input, current_url);
                            draw_browser();
                        } else if (key == '\b' || key == 127) {
                            if (cursor_pos > 0) {
                                // Delete character before cursor
                                for (int i = cursor_pos - 1; url_input[i]; i++) {
                                    url_input[i] = url_input[i + 1];
                                }
                                cursor_pos--;
                                draw_browser();
                            }
                        } else if (key == KEY_LEFT) {
                            if (cursor_pos > 0) cursor_pos--;
                            draw_browser();
                        } else if (key == KEY_RIGHT) {
                            if (url_input[cursor_pos]) cursor_pos++;
                            draw_browser();
                        } else if (key >= 32 && key < 127) {
                            int len = str_len(url_input);
                            if (len < 500) {
                                // Insert character at cursor
                                for (int i = len + 1; i > cursor_pos; i--) {
                                    url_input[i] = url_input[i - 1];
                                }
                                url_input[cursor_pos++] = key;
                                draw_browser();
                            }
                        }
                    } else {
                        // Not editing URL
                        if (key == 'g' || key == 'G') {
                            // Go to URL
                            editing_url = 1;
                            cursor_pos = str_len(url_input);
                            draw_browser();
                        } else if (key == 'r' || key == 'R') {
                            // Reload
                            navigate_internal(current_url, 0);
                        } else if (key == '\b' || key == 127 || key == 'b' || key == 'B') {
                            // Back
                            go_back();
                        } else if (key == KEY_UP || key == 'k') {
                            scroll_offset -= CHAR_H * 3;
                            if (scroll_offset < 0) scroll_offset = 0;
                            draw_browser();
                        } else if (key == KEY_DOWN || key == 'j') {
                            int max_scroll = content_height - (win_h - CONTENT_Y);
                            if (max_scroll < 0) max_scroll = 0;
                            scroll_offset += CHAR_H * 3;
                            if (scroll_offset > max_scroll) scroll_offset = max_scroll;
                            draw_browser();
                        } else if (key == ' ') {
                            // Page down
                            int max_scroll = content_height - (win_h - CONTENT_Y);
                            if (max_scroll < 0) max_scroll = 0;
                            scroll_offset += win_h - CONTENT_Y - CHAR_H * 2;
                            if (scroll_offset > max_scroll) scroll_offset = max_scroll;
                            draw_browser();
                        }
                    }
                    break;
                }

                case WIN_EVENT_MOUSE_DOWN: {
                    int mx = data1;
                    int my = data2;

                    // Click in address bar area
                    if (my < ADDR_BAR_HEIGHT) {
                        if (mx >= 4 && mx < 4 + BACK_BTN_W) {
                            // Back button clicked
                            go_back();
                        } else {
                            // URL bar clicked
                            editing_url = 1;
                            cursor_pos = str_len(url_input);
                            draw_browser();
                        }
                    } else if (scrollbar_h > 0 && mx >= win_w - SCROLLBAR_W) {
                        // Click on scrollbar area
                        if (my >= scrollbar_y && my < scrollbar_y + scrollbar_h) {
                            // Start dragging scrollbar
                            dragging_scrollbar = 1;
                            drag_start_y = my;
                            drag_start_scroll = scroll_offset;
                        } else if (my < scrollbar_y) {
                            // Click above scrollbar - page up
                            scroll_offset -= (win_h - CONTENT_Y - 16);
                            if (scroll_offset < 0) scroll_offset = 0;
                            draw_browser();
                        } else {
                            // Click below scrollbar - page down
                            int max_scroll = content_height - (win_h - CONTENT_Y - 16);
                            if (max_scroll < 0) max_scroll = 0;
                            scroll_offset += (win_h - CONTENT_Y - 16);
                            if (scroll_offset > max_scroll) scroll_offset = max_scroll;
                            draw_browser();
                        }
                    } else if (!editing_url) {
                        // Check for link click
                        for (int i = 0; i < num_link_regions; i++) {
                            link_region_t *lr = &link_regions[i];
                            if (mx >= lr->x && mx < lr->x + lr->w &&
                                my >= lr->y && my < lr->y + lr->h) {
                                // Clicked on a link!
                                char resolved[512];
                                resolve_url(lr->url, resolved, 512);
                                navigate(resolved);
                                break;
                            }
                        }
                    }
                    break;
                }

                case WIN_EVENT_MOUSE_UP:
                    dragging_scrollbar = 0;
                    break;

                case WIN_EVENT_MOUSE_MOVE:
                    if (dragging_scrollbar) {
                        int dy = data2 - drag_start_y;
                        int content_area = win_h - CONTENT_Y - 16;
                        int max_scroll = content_height - content_area;
                        if (max_scroll > 0 && content_area > scrollbar_h) {
                            int scroll_range = content_area - scrollbar_h;
                            scroll_offset = drag_start_scroll + dy * max_scroll / scroll_range;
                            if (scroll_offset < 0) scroll_offset = 0;
                            if (scroll_offset > max_scroll) scroll_offset = max_scroll;
                            draw_browser();
                        }
                    }
                    break;

                case WIN_EVENT_RESIZE:
                    // Re-fetch buffer with new dimensions
                    win_buf = k->window_get_buffer(window_id, &win_w, &win_h);
                    gfx_init(&gfx, win_buf, win_w, win_h, k->font_data);
                    draw_browser();
                    break;
            }
        }

        k->yield();
    }

    free_blocks();
    k->window_destroy(window_id);
    return 0;
}
