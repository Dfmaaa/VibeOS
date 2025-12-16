# mdview.py - Markdown Viewer for VibeOS
# A windowed markdown renderer written in Python
#
# Usage: mpy /home/user/mdview.py [filename.md]

import vibe
import sys

# Colors (1-bit aesthetic)
WHITE = 0x00FFFFFF
BLACK = 0x00000000
GRAY = 0x00888888

# Font is 8x16
CHAR_W = 8
CHAR_H = 16

# Window dimensions
WIN_W = 500
WIN_H = 400
MARGIN = 10
TEXT_W = WIN_W - MARGIN * 2

class MDViewer:
    def __init__(self, filename):
        self.filename = filename
        self.lines = []  # Rendered lines: [(text, style), ...]
        self.scroll = 0
        self.max_scroll = 0
        self.wid = -1

    def load_file(self):
        """Load and parse markdown file."""
        f = vibe.open(self.filename)
        if f is None:
            print("Error: Cannot open " + self.filename)
            return False

        size = vibe.file_size(f)
        data = vibe.read(f, size, 0)
        if data is None:
            print("Error: Cannot read file")
            return False

        # Convert bytes to string
        # MicroPython: bytes can be converted via ''.join(chr(b) for b in data)
        text = ''
        for b in data:
            text = text + chr(b)
        self.parse_markdown(text)
        return True

    def parse_markdown(self, text):
        """Parse markdown into rendered lines."""
        self.lines = []
        raw_lines = text.replace('\r', '').split('\n')

        in_code_block = False

        for line in raw_lines:
            # Code block toggle
            if line.strip().startswith('```'):
                in_code_block = not in_code_block
                self.lines.append(('', 'normal'))
                continue

            # Inside code block
            if in_code_block:
                self.lines.append((line, 'code'))
                continue

            # Horizontal rule
            if line.strip() in ('---', '***', '___'):
                self.lines.append(('---', 'hr'))
                continue

            # Headers
            if line.startswith('### '):
                self.add_wrapped(line[4:], 'h3')
                continue
            if line.startswith('## '):
                self.add_wrapped(line[3:], 'h2')
                continue
            if line.startswith('# '):
                self.add_wrapped(line[2:], 'h1')
                continue

            # List items
            if line.strip().startswith('- ') or line.strip().startswith('* '):
                indent = len(line) - len(line.lstrip())
                content = line.strip()[2:]
                prefix = '  ' * (indent // 2) + '* '
                self.add_wrapped(prefix + content, 'list')
                continue

            # Numbered list
            if len(line.strip()) > 2 and line.strip()[0].isdigit() and line.strip()[1] == '.':
                self.add_wrapped(line.strip(), 'list')
                continue

            # Empty line
            if not line.strip():
                self.lines.append(('', 'normal'))
                continue

            # Normal paragraph with word wrap
            self.add_wrapped(line, 'normal')

        # Calculate max scroll
        visible_lines = (WIN_H - MARGIN * 2) // CHAR_H
        self.max_scroll = max(0, len(self.lines) - visible_lines)

    def add_wrapped(self, text, style):
        """Add text with word wrapping."""
        max_chars = (TEXT_W // CHAR_W) - 1

        # Handle inline styles (bold, italic, code)
        # For simplicity, just strip the markers for display
        text = text.replace('**', '').replace('*', '').replace('`', '')

        while len(text) > max_chars:
            # Find last space before limit
            split = text[:max_chars].rfind(' ')
            if split == -1:
                split = max_chars
            self.lines.append((text[:split], style))
            text = text[split:].lstrip()

        if text:
            self.lines.append((text, style))

    def draw(self):
        """Draw the markdown content to window."""
        # Clear window
        vibe.window_fill_rect(self.wid, 0, 0, WIN_W, WIN_H, WHITE)

        y = MARGIN
        visible_lines = (WIN_H - MARGIN * 2) // CHAR_H

        for i in range(self.scroll, min(self.scroll + visible_lines, len(self.lines))):
            text, style = self.lines[i]
            x = MARGIN

            if style == 'h1':
                # Draw header with underline
                vibe.window_draw_string(self.wid, x, y, text.upper(), BLACK, WHITE)
                vibe.window_draw_hline(self.wid, x, y + CHAR_H - 2, len(text) * CHAR_W, BLACK)
            elif style == 'h2':
                vibe.window_draw_string(self.wid, x, y, text, BLACK, WHITE)
                vibe.window_draw_hline(self.wid, x, y + CHAR_H - 2, len(text) * CHAR_W, GRAY)
            elif style == 'h3':
                vibe.window_draw_string(self.wid, x, y, '> ' + text, BLACK, WHITE)
            elif style == 'hr':
                # Draw horizontal rule
                vibe.window_draw_hline(self.wid, MARGIN, y + CHAR_H // 2, TEXT_W, GRAY)
            elif style == 'code':
                # Draw code with gray background
                vibe.window_fill_rect(self.wid, MARGIN - 2, y, TEXT_W + 4, CHAR_H, 0x00DDDDDD)
                vibe.window_draw_string(self.wid, x, y, text, BLACK, 0x00DDDDDD)
            elif style == 'list':
                vibe.window_draw_string(self.wid, x, y, text, BLACK, WHITE)
            else:
                vibe.window_draw_string(self.wid, x, y, text, BLACK, WHITE)

            y += CHAR_H

        # Draw scroll indicator if needed
        if self.max_scroll > 0:
            bar_h = WIN_H - MARGIN * 2
            thumb_h = max(20, bar_h * visible_lines // len(self.lines))
            thumb_y = MARGIN + (bar_h - thumb_h) * self.scroll // self.max_scroll
            vibe.window_fill_rect(self.wid, WIN_W - 8, MARGIN, 6, bar_h, 0x00CCCCCC)
            vibe.window_fill_rect(self.wid, WIN_W - 8, thumb_y, 6, thumb_h, GRAY)

        vibe.window_invalidate(self.wid)

    def run(self):
        """Main event loop."""
        # Create window
        self.wid = vibe.window_create(50, 50, WIN_W, WIN_H, "Markdown Viewer")
        if self.wid < 0:
            print("Error: Cannot create window")
            return 1

        # Load and parse file
        if not self.load_file():
            vibe.window_destroy(self.wid)
            return 1

        # Set title to filename
        name = self.filename.split('/')[-1]
        vibe.window_set_title(self.wid, name)

        # Initial draw
        self.draw()

        # Event loop
        running = True
        while running:
            event = vibe.window_poll(self.wid)

            if event:
                etype, d1, d2, d3 = event

                if etype == vibe.WIN_EVENT_CLOSE:
                    running = False

                elif etype == vibe.WIN_EVENT_KEY:
                    key = d1
                    # Page Up / Page Down / Arrow keys
                    if key == 0x109:  # KEY_PGUP
                        self.scroll = max(0, self.scroll - 10)
                        self.draw()
                    elif key == 0x10A:  # KEY_PGDN
                        self.scroll = min(self.max_scroll, self.scroll + 10)
                        self.draw()
                    elif key == 0x101:  # KEY_DOWN
                        self.scroll = min(self.max_scroll, self.scroll + 1)
                        self.draw()
                    elif key == 0x100:  # KEY_UP
                        self.scroll = max(0, self.scroll - 1)
                        self.draw()
                    elif key == ord('q') or key == 27:  # q or ESC
                        running = False

            vibe.sched_yield()

        vibe.window_destroy(self.wid)
        return 0


def main():
    # Check for filename argument
    if len(sys.argv) < 2:
        print("Usage: mpy mdview.py <file.md>")
        print("")
        print("Markdown Viewer for VibeOS")
        print("Supports: headers, lists, code blocks, horizontal rules")
        print("")
        print("Keys:")
        print("  Up/Down   - Scroll one line")
        print("  PgUp/PgDn - Scroll 10 lines")
        print("  q/ESC     - Quit")
        return 1

    viewer = MDViewer(sys.argv[1])
    return viewer.run()


main()
