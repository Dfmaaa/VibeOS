/*
 * VibeOS Web Browser
 *
 * A simple text-mode web browser with basic HTML rendering.
 *
 * Usage: browser [url]
 * Example: browser http://example.com/
 */

#include "../../lib/vibe.h"
#include "../../lib/gfx.h"
#include "str.h"
#include "url.h"
#include "http.h"
#include "html.h"

static kapi_t *k;

// Clickable link regions for hit testing
typedef struct {
    int x, y, w, h;      // Bounding box (relative to content, not scroll)
    char url[512];
} link_region_t;

#define MAX_LINK_REGIONS 256
static link_region_t link_regions[MAX_LINK_REGIONS];
static int num_link_regions = 0;

// ============ Browser UI ============

#define WIN_WIDTH 600
#define WIN_HEIGHT 400
#define ADDR_BAR_HEIGHT 24
#define CONTENT_Y (ADDR_BAR_HEIGHT + 2)
#define CHAR_W 8
#define CHAR_H 16
#define MARGIN 8

// TTF font sizes for different elements
#define FONT_SIZE_H1 28
#define FONT_SIZE_H2 24
#define FONT_SIZE_H3 20
#define FONT_SIZE_H4 18
#define FONT_SIZE_BODY 16
#define FONT_SIZE_SMALL 14

// Check if TTF is available
static int use_ttf = 0;

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
#define BACK_BTN_W 24

// Get font size for a text block (now uses CSS font_size)
static int get_font_size(text_block_t *block) {
    // Use CSS font size if set, otherwise fall back to heading-based
    if (block->font_size > 0) {
        return block->font_size;
    }
    if (block->is_heading) {
        switch (block->is_heading) {
            case 1: return FONT_SIZE_H1;
            case 2: return FONT_SIZE_H2;
            case 3: return FONT_SIZE_H3;
            case 4: return FONT_SIZE_H4;
            default: return FONT_SIZE_BODY;
        }
    }
    return FONT_SIZE_BODY;
}

// Get font style for a text block
static int get_font_style(text_block_t *block) {
    int style = TTF_STYLE_NORMAL;
    if (block->is_bold || block->is_heading) style |= TTF_STYLE_BOLD;
    if (block->is_italic) style |= TTF_STYLE_ITALIC;
    return style;
}

// Get line height for font size
static int get_line_height(int font_size) {
    return font_size + 4;  // font size + some padding
}

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
    int base_margin = MARGIN;
    int content_width = win_w - MARGIN * 2 - SCROLLBAR_W;
    int current_x = base_margin;  // Track horizontal position across inline blocks
    int current_line_height = CHAR_H;

    text_block_t *block = get_blocks_head();
    while (block) {
        if (y > win_h) break;

        // Skip hidden blocks (CSS display:none or visibility:hidden)
        if (block->is_hidden) {
            block = block->next;
            continue;
        }

        // Get font parameters for this block
        int font_size = use_ttf ? get_font_size(block) : CHAR_H;
        int font_style = use_ttf ? get_font_style(block) : 0;
        int line_height = use_ttf ? get_line_height(font_size) : CHAR_H;

        // Handle newline blocks
        if (block->is_newline) {
            y += current_line_height;
            current_x = base_margin;
            current_line_height = CHAR_H;
            block = block->next;
            continue;
        }

        // Skip empty blocks
        if (!block->text) {
            block = block->next;
            continue;
        }

        const char *text = block->text;
        int len = str_len(text);

        // Update line height if this block is taller
        if (line_height > current_line_height) {
            current_line_height = line_height;
        }

        // Adjust margin for blockquotes, list items, and CSS margin
        int left_margin = base_margin;
        if (block->is_blockquote) {
            left_margin += 16;  // Indent blockquotes
        }
        if (block->is_list_item) {
            left_margin += 24;  // Indent list items
        }
        // Add CSS margin-left
        if (block->margin_left > 0) {
            left_margin += block->margin_left;
        }

        // Calculate max width for this block
        int max_width = content_width - (left_margin - base_margin);

        // For TTF, estimate chars per line based on average char width
        // TTF fonts average ~0.5 to 0.6 of font_size per character
        int avg_char_width = use_ttf ? (font_size * 6 / 10) : CHAR_W;
        if (avg_char_width < 4) avg_char_width = 4;
        int line_max = max_width / avg_char_width;

        // Track if we're on first line of block (for bullet rendering)
        int first_line = 1;

        // For preformatted text, don't word wrap
        int do_word_wrap = !block->is_preformatted;

        // Word wrap (or not for preformatted)
        int pos = 0;
        while (pos < len) {
            // Find line break point
            int line_len = 0;
            int last_space = -1;

            if (block->is_preformatted) {
                // For preformatted, break only at newlines
                while (pos + line_len < len && text[pos + line_len] != '\n') {
                    line_len++;
                }
            } else {
                // Use a conservative estimate for line length
                while (pos + line_len < len && line_len < line_max) {
                    if (text[pos + line_len] == '\n') break;
                    if (text[pos + line_len] == ' ') last_space = line_len;
                    line_len++;
                }

                // Break at word boundary if possible
                if (do_word_wrap && pos + line_len < len && last_space > 0 && line_len >= line_max) {
                    line_len = last_space + 1;
                }
            }

            // Draw line if visible
            if (y + CHAR_H > CONTENT_Y && y < win_h - 16) {
                // Determine foreground and background colors from CSS
                uint32_t fg = block->color ? block->color : COLOR_BLACK;
                uint32_t bg = (block->bg_color && block->bg_color != 0xFFFFFF) ? block->bg_color : COLOR_WHITE;

                // Override for special elements
                if (block->is_link) {
                    fg = 0x000000FF;  // Blue for links (CSS may override)
                }
                if (block->is_image) {
                    fg = 0x00666666;  // Gray for image placeholders
                    bg = 0x00EEEEEE;  // Light gray background
                }
                if (block->is_preformatted && bg == COLOR_WHITE) {
                    bg = 0x00F0F0F0;  // Slight gray background for code
                }

                // Draw blockquote indicator
                if (block->is_blockquote) {
                    gfx_fill_rect(&gfx, base_margin, y, 3, CHAR_H, 0x00888888);
                }

                // Draw list bullet or number on first line
                if (block->is_list_item && first_line) {
                    if (block->is_list_item == -1) {
                        // Unordered list - bullet
                        gfx_draw_char(&gfx, base_margin, y, '*', COLOR_BLACK, COLOR_WHITE);
                    } else {
                        // Ordered list - number
                        int num = block->is_list_item;
                        char num_buf[8];
                        int i = 0;
                        // Convert number to string (reversed)
                        do {
                            num_buf[i++] = '0' + (num % 10);
                            num /= 10;
                        } while (num > 0);
                        // Draw in correct order
                        int nx = base_margin;
                        while (i > 0) {
                            gfx_draw_char(&gfx, nx, y, num_buf[--i], COLOR_BLACK, COLOR_WHITE);
                            nx += CHAR_W;
                        }
                        gfx_draw_char(&gfx, nx, y, '.', COLOR_BLACK, COLOR_WHITE);
                    }
                }

                // Draw background for special blocks
                if (block->is_image || block->is_preformatted) {
                    int line_width = 0;
                    for (int i = 0; i < line_len && text[pos + i] != '\n'; i++) {
                        line_width++;
                    }
                    gfx_fill_rect(&gfx, left_margin - 2, y, line_width * CHAR_W + 4, CHAR_H, bg);
                }

                // Use current_x for inline continuation, left_margin for block start
                int start_x;
                if (current_x > left_margin) {
                    // Continuing on same line - add space between blocks
                    start_x = current_x + CHAR_W;
                } else {
                    start_x = left_margin;
                }

                // Draw text - use TTF if available
                int actual_chars = 0;
                int actual_width = 0;
                int x = start_x;

                // Build the line to draw
                char line_buf[256];
                int line_buf_len = 0;
                for (int i = 0; i < line_len && text[pos + i] != '\n' && line_buf_len < 255; i++) {
                    line_buf[line_buf_len++] = text[pos + i];
                    actual_chars++;
                }
                line_buf[line_buf_len] = '\0';

                if (use_ttf && k->ttf_is_ready && k->ttf_is_ready()) {
                    // TTF rendering - render word by word for proper wrapping
                    int right_edge = win_w - SCROLLBAR_W - MARGIN;
                    int word_start = 0;
                    actual_width = 0;

                    while (word_start < line_buf_len) {
                        // Find next word
                        int word_end = word_start;
                        while (word_end < line_buf_len && line_buf[word_end] != ' ') word_end++;

                        // Include trailing space
                        if (word_end < line_buf_len && line_buf[word_end] == ' ') word_end++;

                        // Build word string
                        char word[128];
                        int word_len = word_end - word_start;
                        if (word_len > 127) word_len = 127;
                        for (int i = 0; i < word_len; i++) word[i] = line_buf[word_start + i];
                        word[word_len] = '\0';

                        // Estimate word width (no measure function available)
                        int word_width = word_len * avg_char_width;

                        // Check if we need to wrap
                        if (x + word_width > right_edge && x > left_margin) {
                            y += line_height;
                            x = left_margin;
                        }

                        // Draw the word
                        int drawn = gfx_draw_ttf_string(&gfx, k, x, y, word,
                                                        font_size, font_style, fg, bg);
                        x += drawn;
                        actual_width += drawn;
                        word_start = word_end;
                    }
                } else {
                    // Bitmap font fallback
                    for (int i = 0; i < line_buf_len; i++) {
                        if (x + CHAR_W > win_w - SCROLLBAR_W - MARGIN) {
                            y += CHAR_H;
                            x = left_margin;
                        }
                        gfx_draw_char(&gfx, x, y, line_buf[i], fg, bg);
                        x += CHAR_W;
                    }
                    actual_width = line_buf_len * CHAR_W;
                }
                current_x = x;  // Save position for next inline block

                // Underline for links
                if (block->is_link) {
                    int ul_y = use_ttf ? y + line_height - 2 : y + CHAR_H - 2;
                    gfx_fill_rect(&gfx, start_x, ul_y, actual_width, 1, fg);
                }

                // Register link region for hit testing
                if (block->is_link && block->link_url && num_link_regions < MAX_LINK_REGIONS && actual_width > 0) {
                    link_region_t *lr = &link_regions[num_link_regions++];
                    lr->x = start_x;
                    lr->y = y;
                    lr->w = actual_width;
                    lr->h = use_ttf ? line_height : CHAR_H;
                    str_ncpy(lr->url, block->link_url, 511);
                }

                // Underline for h1 headings (TTF already has bold, so skip underline)
                if (block->is_heading == 1 && !use_ttf) {
                    gfx_fill_rect(&gfx, left_margin, y + CHAR_H - 2,
                                  actual_width, 2, COLOR_BLACK);
                }

                // Draw image box border
                if (block->is_image) {
                    int box_h = use_ttf ? line_height : CHAR_H;
                    gfx_draw_rect(&gfx, left_margin - 3, y - 1,
                                  actual_width + 6, box_h + 2, 0x00888888);
                }
            }

            pos += line_len;
            first_line = 0;

            // Skip newline in text and advance y
            if (pos < len && text[pos] == '\n') {
                pos++;
                y += use_ttf ? line_height : CHAR_H;
                current_x = left_margin;
            }
        }

        // Extra space after paragraphs and special blocks
        if (block->is_paragraph || block->is_heading || block->is_blockquote || block->is_image) {
            y += use_ttf ? line_height / 2 : CHAR_H / 2;
            current_x = base_margin;
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
    if (get_blocks_head()) {
        gfx_draw_string(&gfx, 4, win_h - 16, "Ready", COLOR_BLACK, 0x00DDDDDD);
    } else if (current_url[0]) {
        gfx_draw_string(&gfx, 4, win_h - 16, "Loading...", COLOR_BLACK, 0x00DDDDDD);
    } else {
        gfx_draw_string(&gfx, 4, win_h - 16, "Enter URL and press Enter", COLOR_BLACK, 0x00DDDDDD);
    }

    k->window_invalidate(window_id);
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

// Helper to show an error message
static void show_error(const char *msg) {
    // Create a simple error page as HTML
    char html[256];
    char *p = html;
    const char *s = "<html><body><h1>Error</h1><p>";
    while (*s) *p++ = *s++;
    while (*msg) *p++ = *msg++;
    s = "</p></body></html>";
    while (*s) *p++ = *s++;
    *p = '\0';

    parse_html(html, p - html);
    dom_to_blocks();
}

static void navigate_internal(const char *url, int add_to_history) {
    (void)add_to_history;
    k->uart_puts("[NAV] start\n");
    str_cpy(current_url, url);
    str_cpy(url_input, url);
    free_blocks();
    num_link_regions = 0;
    scroll_offset = 0;
    draw_browser();

    url_t parsed;
    k->uart_puts("[NAV] parsing url\n");
    if (parse_url(url, &parsed) < 0) {
        show_error("Invalid URL");
        draw_browser();
        return;
    }

    k->uart_puts("[NAV] malloc\n");
    char *response = k->malloc(131072);  // 128KB
    if (!response) {
        show_error("Out of memory");
        draw_browser();
        return;
    }

    http_response_t resp;
    int redirects = 0;

    while (1) {
        k->uart_puts("[NAV] http_get\n");
        int len = http_get(k, &parsed, response, 131072, &resp);

        if (len <= 0) {
            show_error("No response from server");
            break;
        }

        k->uart_puts("[NAV] got response\n");
        if (is_redirect(resp.status_code) && resp.location[0] && redirects < 5) {
            redirects++;
            // Check if it's a relative URL (starts with /)
            if (resp.location[0] == '/') {
                // Just update path, keep same host and protocol
                str_cpy(parsed.path, resp.location);
            } else {
                // Parse new URL (might switch http->https or vice versa)
                parse_url(resp.location, &parsed);
            }
            continue;
        }

        // For non-200 responses, still try to render the body (many sites return HTML error pages)
        if (resp.header_len > 0 && resp.header_len < len) {
            k->uart_puts("[NAV] parse_html\n");
            parse_html(response + resp.header_len, len - resp.header_len);
            k->uart_puts("[NAV] dom_to_blocks\n");
            dom_to_blocks();  // Convert DOM tree to flat blocks for rendering
            k->uart_puts("[NAV] done parsing\n");
        }
        break;
    }

    k->uart_puts("[NAV] free\n");
    k->free(response);
    k->uart_puts("[NAV] draw\n");
    draw_browser();
    k->uart_puts("[NAV] done\n");
}

int main(kapi_t *kapi, int argc, char **argv) {
    k = kapi;
    html_set_kapi(kapi);

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

    // Check if TTF fonts are available
    if (k->ttf_is_ready && k->ttf_is_ready()) {
        use_ttf = 1;
    }

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
                                resolve_url(lr->url, current_url, resolved, 512);
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
