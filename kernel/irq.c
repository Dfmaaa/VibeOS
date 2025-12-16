/*
 * VibeOS Interrupt Handling - Shared Code
 *
 * Platform-specific drivers are in hal/qemu/irq.c and hal/pizero2w/irq.c.
 * This file contains:
 * - Exception handlers (sync, FIQ, SError) shared by all platforms
 * - Legacy API wrappers for QEMU compatibility
 */

#include "irq.h"
#include "printf.h"
#include "hal/hal.h"
#include "fb.h"
#include "process.h"

// Direct UART output (always works, even if printf goes to screen)
extern void uart_puts(const char *s);
extern void uart_putc(char c);

static void uart_puthex(uint64_t val) {
    const char *hex = "0123456789ABCDEF";
    uart_puts("0x");
    for (int i = 60; i >= 0; i -= 4) {
        uart_putc(hex[(val >> i) & 0xF]);
    }
}

// ============================================================================
// Legacy API Wrappers (for QEMU code compatibility)
// These call through to HAL functions
// ============================================================================

void irq_init(void) {
    hal_irq_init();
}

void irq_enable(void) {
    hal_irq_enable();
}

void irq_disable(void) {
    hal_irq_disable();
}

void irq_enable_irq(uint32_t irq) {
    hal_irq_enable_irq(irq);
}

void irq_disable_irq(uint32_t irq) {
    hal_irq_disable_irq(irq);
}

void irq_register_handler(uint32_t irq, irq_handler_t handler) {
    hal_irq_register_handler(irq, handler);
}

void timer_init(uint32_t interval_ms) {
    hal_timer_init(interval_ms);
}

uint64_t timer_get_ticks(void) {
    return hal_timer_get_ticks();
}

void timer_set_interval(uint32_t interval_ms) {
    hal_timer_set_interval(interval_ms);
}

void wfi(void) {
    asm volatile("wfi");
}

void sleep_ms(uint32_t ms) {
    // Timer runs at 100Hz (10ms per tick)
    uint64_t ticks_to_wait = (ms + 9) / 10;
    if (ticks_to_wait == 0) ticks_to_wait = 1;

    uint64_t target = hal_timer_get_ticks() + ticks_to_wait;
    while (hal_timer_get_ticks() < target) {
        wfi();
    }
}

// ============================================================================
// Shared Exception Handlers (used by all platforms)
// Called from vectors.S
// ============================================================================

// WSOD - White Screen of Death
// The final art piece of VibeOS

static const char *wsod_quotes[] = {
    "\"Imagination is more important than knowledge.\" - Albert Einstein",
    "\"The only real mistake is the one from which we learn nothing.\" - Henry Ford",
    "\"Death solves all problems. No man, no problem.\" - Joseph Stalin",
    "\"In the middle of difficulty lies opportunity.\" - Albert Einstein",
    "\"One death is a tragedy; a million is a statistic.\" - Joseph Stalin",
    "\"Stay hungry, stay foolish.\" - Steve Jobs",
    "\"The best way to predict the future is to invent it.\" - Alan Kay",
    "\"First, solve the problem. Then, write the code.\" - John Johnson",
    "\"It works on my machine.\" - Every Developer Ever",
    "\"Have you tried turning it off and on again?\" - IT Support",
    "\"There are only two hard things: cache invalidation and naming things.\" - Phil Karlton",
    "\"99 little bugs in the code, take one down, patch it around... 127 bugs in the code.\" - Anonymous",
    "\"The vibes were, in fact, not immaculate.\" - VibeOS",
    "\"I have not failed. I've just found 10,000 ways that won't work.\" - Thomas Edison",
    "\"Reality is merely an illusion, albeit a very persistent one.\" - Albert Einstein",
};
#define WSOD_QUOTE_COUNT (sizeof(wsod_quotes) / sizeof(wsod_quotes[0]))

static const char *wsod_art[] = {
    "",
    "         db    db d888888b d8888b. d88888b .d8888.                        ",
    "         88    88   `88'   88  `8D 88'     88'  YP                        ",
    "         Y8    8P    88    88oooY' 88ooooo `8bo.                          ",
    "         `8b  d8'    88    88~~~b. 88~~~~~   `Y8b.                        ",
    "          `8bd8'    .88.   88   8D 88.     db   8D                        ",
    "            YP    Y888888P Y8888P' Y88888P `8888Y'                        ",
    "",
    "                     d8b   db  .d88b.  d888888b                           ",
    "                     888o  88 .8P  Y8.   `88'                             ",
    "                     88V8o 88 88    88    88                              ",
    "                     88 V8o88 88    88    88                              ",
    "                     88  V888 `8b  d8'   .88.                             ",
    "                     VP   V8P  `Y88P'  Y888888P                           ",
    "",
    " d888888b .88b  d88. .88b  d88.  .d8b.   .o88b. db    db db       .d8b.  d888888b d88888b",
    "   `88'   88'YbdP`88 88'YbdP`88 d8' `8b d8P  Y8 88    88 88      d8' `8b `~~88~~' 88'    ",
    "    88    88  88  88 88  88  88 88ooo88 8P      88    88 88      88ooo88    88    88ooooo",
    "    88    88  88  88 88  88  88 88~~~88 8b      88    88 88      88~~~88    88    88~~~~~",
    "   .88.   88  88  88 88  88  88 88   88 Y8b  d8 88b  d88 88booo. 88   88    88    88.    ",
    " Y888888P YP  YP  YP YP  YP  YP YP   YP  `Y88P' ~Y8888P' Y88888P YP   YP    YP    Y88888P",
    "",
    NULL
};

static void wsod_draw_text(int x, int y, const char *s) {
    while (*s) {
        fb_draw_char(x, y, *s, COLOR_BLACK, COLOR_WHITE);
        x += 8;
        s++;
    }
}

static void wsod_draw_line(int y) {
    for (uint32_t x = 40; x < fb_width - 40; x++) {
        fb_put_pixel(x, y, COLOR_BLACK);
    }
}

static const char *get_exception_name(uint32_t ec) {
    switch (ec) {
        case 0x00: return "Unknown";
        case 0x01: return "Trapped WFI/WFE";
        case 0x0E: return "Illegal State";
        case 0x15: return "SVC (Syscall)";
        case 0x20: return "Instruction Abort (Lower EL)";
        case 0x21: return "Instruction Abort";
        case 0x22: return "PC Alignment Fault";
        case 0x24: return "Data Abort (Lower EL)";
        case 0x25: return "Data Abort";
        case 0x26: return "SP Alignment Fault";
        case 0x2C: return "FP Exception";
        default:   return "Exception";
    }
}

static void wsod_hex(char *buf, uint64_t val, int digits) {
    const char *hex = "0123456789ABCDEF";
    buf[0] = '0';
    buf[1] = 'x';
    for (int i = 0; i < digits; i++) {
        buf[2 + digits - 1 - i] = hex[val & 0xF];
        val >>= 4;
    }
    buf[2 + digits] = '\0';
}

void handle_sync_exception(uint64_t esr, uint64_t elr, uint64_t far) {
    uint32_t ec = (esr >> 26) & 0x3F;
    uint32_t iss = esr & 0x1FFFFFF;

    // Always print to UART for serial debugging
    uart_puts("\n\n");
    uart_puts("========================================\n");
    uart_puts("  KERNEL PANIC: ");
    uart_puts(get_exception_name(ec));
    uart_puts("\n========================================\n");
    uart_puts("  Fault Address:  "); uart_puthex(far); uart_puts("\n");
    uart_puts("  Return Address: "); uart_puthex(elr); uart_puts("\n");
    uart_puts("  ESR:            "); uart_puthex(esr); uart_puts("\n");
    if (ec == 0x24 || ec == 0x25 || ec == 0x20 || ec == 0x21) {
        uart_puts("  Access Type:    ");
        uart_puts((iss & (1 << 6)) ? "Write" : "Read");
        uart_puts("\n");
    }
    if (current_process) {
        uart_puts("  Process:        ");
        uart_puts(current_process->name);
        uart_puts("\n");
    }
    uart_puts("========================================\n");

    // Draw WSOD if framebuffer is available
    if (fb_base && fb_width > 0 && fb_height > 0) {
        // Reset hardware scroll to top (Pi has hardware scrolling)
        hal_fb_set_scroll_offset(0);

        // Fill screen white
        fb_clear(COLOR_WHITE);

        // Draw the ASCII art centered
        int art_y = 30;
        for (int i = 0; wsod_art[i] != NULL; i++) {
            const char *line = wsod_art[i];
            int len = 0;
            while (line[len]) len++;
            int art_x = (fb_width - len * 8) / 2;
            if (art_x < 0) art_x = 8;
            wsod_draw_text(art_x, art_y, line);
            art_y += 16;
        }

        // Draw separator line
        int info_y = art_y + 20;
        wsod_draw_line(info_y);
        info_y += 20;

        // Two-column layout: exception info on left, process info on right
        int left_col = 60;
        int right_col = fb_width / 2 + 40;
        char buf[64];

        // Left column - Exception info
        wsod_draw_text(left_col, info_y, "Exception:");
        wsod_draw_text(left_col + 136, info_y, get_exception_name(ec));

        // Right column - Process info (if available)
        if (current_process) {
            wsod_draw_text(right_col, info_y, "Process:");
            wsod_draw_text(right_col + 80, info_y, current_process->name);
        }
        info_y += 20;

        wsod_draw_text(left_col, info_y, "Fault Address:");
        wsod_hex(buf, far, 16);
        wsod_draw_text(left_col + 136, info_y, buf);

        if (current_process) {
            wsod_draw_text(right_col, info_y, "PID:");
            char pid_buf[16];
            int pid = current_process->pid;
            int idx = 0;
            if (pid == 0) {
                pid_buf[idx++] = '0';
            } else {
                char tmp[16];
                int ti = 0;
                while (pid > 0) {
                    tmp[ti++] = '0' + (pid % 10);
                    pid /= 10;
                }
                while (ti > 0) pid_buf[idx++] = tmp[--ti];
            }
            pid_buf[idx] = '\0';
            wsod_draw_text(right_col + 80, info_y, pid_buf);
        }
        info_y += 20;

        wsod_draw_text(left_col, info_y, "Return Address:");
        wsod_hex(buf, elr, 16);
        wsod_draw_text(left_col + 136, info_y, buf);
        info_y += 20;

        wsod_draw_text(left_col, info_y, "ESR:");
        wsod_hex(buf, esr, 16);
        wsod_draw_text(left_col + 136, info_y, buf);

        if (ec == 0x24 || ec == 0x25 || ec == 0x20 || ec == 0x21) {
            wsod_draw_text(right_col, info_y, "Access:");
            wsod_draw_text(right_col + 80, info_y,
                          (iss & (1 << 6)) ? "Write" : "Read");
        }
        info_y += 20;

        // Random quote - use multiple entropy sources
        uint64_t cntpct;
        asm volatile("mrs %0, cntpct_el0" : "=r"(cntpct));  // High-res system counter
        uint64_t entropy = cntpct ^ (far * 31) ^ (elr * 17) ^ (esr * 13);
        int quote_idx = (entropy >> 8) % WSOD_QUOTE_COUNT;  // Use middle bits
        const char *quote = wsod_quotes[quote_idx];

        // Bottom section
        info_y = fb_height - 80;
        wsod_draw_line(info_y);
        info_y += 16;

        // Draw quote centered
        int quote_len = 0;
        while (quote[quote_len]) quote_len++;
        int quote_x = (fb_width - quote_len * 8) / 2;
        if (quote_x < 8) quote_x = 8;
        wsod_draw_text(quote_x, info_y, quote);
        info_y += 24;

        // System halted message
        const char *msg = "System halted. Please restart your computer.";
        int msg_len = 0;
        while (msg[msg_len]) msg_len++;
        int msg_x = (fb_width - msg_len * 8) / 2;
        wsod_draw_text(msg_x, info_y, msg);
    }

    hal_irq_disable();
    while (1) {
        asm volatile("wfi");
    }
}

void handle_fiq(void) {
    printf("[IRQ] FIQ received (unexpected)\n");
}

void handle_serror(uint64_t esr) {
    // Always print to UART for serial debugging
    uart_puts("\n\n");
    uart_puts("========================================\n");
    uart_puts("  KERNEL PANIC: SError (Async Abort)\n");
    uart_puts("========================================\n");
    uart_puts("  ESR: "); uart_puthex(esr); uart_puts("\n");
    if (current_process) {
        uart_puts("  Process: ");
        uart_puts(current_process->name);
        uart_puts("\n");
    }
    uart_puts("========================================\n");

    // Draw WSOD
    if (fb_base && fb_width > 0 && fb_height > 0) {
        // Reset hardware scroll to top (Pi has hardware scrolling)
        hal_fb_set_scroll_offset(0);

        fb_clear(COLOR_WHITE);

        int art_y = 30;
        for (int i = 0; wsod_art[i] != NULL; i++) {
            const char *line = wsod_art[i];
            int len = 0;
            while (line[len]) len++;
            int art_x = (fb_width - len * 8) / 2;
            if (art_x < 0) art_x = 8;
            wsod_draw_text(art_x, art_y, line);
            art_y += 16;
        }

        int info_y = art_y + 20;
        wsod_draw_line(info_y);
        info_y += 20;

        int left_col = 60;
        int right_col = fb_width / 2 + 40;
        char buf[64];

        wsod_draw_text(left_col, info_y, "Exception:");
        wsod_draw_text(left_col + 136, info_y, "SError (Async Abort)");

        if (current_process) {
            wsod_draw_text(right_col, info_y, "Process:");
            wsod_draw_text(right_col + 80, info_y, current_process->name);
        }
        info_y += 20;

        wsod_draw_text(left_col, info_y, "ESR:");
        wsod_hex(buf, esr, 16);
        wsod_draw_text(left_col + 136, info_y, buf);
        info_y += 20;

        // Random quote - use multiple entropy sources
        uint64_t cntpct;
        asm volatile("mrs %0, cntpct_el0" : "=r"(cntpct));
        uint64_t entropy = cntpct ^ (esr * 31);
        int quote_idx = (entropy >> 8) % WSOD_QUOTE_COUNT;
        const char *quote = wsod_quotes[quote_idx];

        info_y = fb_height - 80;
        wsod_draw_line(info_y);
        info_y += 16;

        int quote_len = 0;
        while (quote[quote_len]) quote_len++;
        int quote_x = (fb_width - quote_len * 8) / 2;
        if (quote_x < 8) quote_x = 8;
        wsod_draw_text(quote_x, info_y, quote);
        info_y += 24;

        const char *msg = "System halted. Please restart your computer.";
        int msg_len = 0;
        while (msg[msg_len]) msg_len++;
        int msg_x = (fb_width - msg_len * 8) / 2;
        wsod_draw_text(msg_x, info_y, msg);
    }

    hal_irq_disable();
    while (1) {
        asm volatile("wfi");
    }
}
