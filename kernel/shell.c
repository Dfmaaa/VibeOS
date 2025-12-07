/*
 * VibeOS Shell
 *
 * Simple command-line shell with built-in commands.
 */

#include "shell.h"
#include "console.h"
#include "keyboard.h"
#include "string.h"
#include "printf.h"
#include "fb.h"
#include "vfs.h"
#include "process.h"
#include "vi.h"
#include "snake.h"
#include "tetris.h"
#include <stddef.h>
#include <stdint.h>

#define MAX_CMD_LEN 256
#define MAX_ARGS 16

// Command buffer
static char cmd_buffer[MAX_CMD_LEN];
static int cmd_pos = 0;

// External memory functions
extern size_t memory_free(void);
extern size_t memory_used(void);


// ============ Command Handlers ============

static void cmd_help(void) {
    console_puts("Available commands:\n");

    console_set_color(COLOR_AMBER, COLOR_BLACK);
    console_puts(" Filesystem:\n");
    console_set_color(COLOR_WHITE, COLOR_BLACK);

    console_puts("  ");
    console_set_color(COLOR_GREEN, COLOR_BLACK);
    console_puts("ls");
    console_set_color(COLOR_WHITE, COLOR_BLACK);
    console_puts(" [path]     - List directory contents\n");

    console_puts("  ");
    console_set_color(COLOR_GREEN, COLOR_BLACK);
    console_puts("cd");
    console_set_color(COLOR_WHITE, COLOR_BLACK);
    console_puts(" <path>     - Change directory\n");

    console_puts("  ");
    console_set_color(COLOR_GREEN, COLOR_BLACK);
    console_puts("pwd");
    console_set_color(COLOR_WHITE, COLOR_BLACK);
    console_puts("            - Print working directory\n");

    console_puts("  ");
    console_set_color(COLOR_GREEN, COLOR_BLACK);
    console_puts("mkdir");
    console_set_color(COLOR_WHITE, COLOR_BLACK);
    console_puts(" <dir>   - Create directory\n");

    console_puts("  ");
    console_set_color(COLOR_GREEN, COLOR_BLACK);
    console_puts("touch");
    console_set_color(COLOR_WHITE, COLOR_BLACK);
    console_puts(" <file>  - Create empty file\n");

    console_puts("  ");
    console_set_color(COLOR_GREEN, COLOR_BLACK);
    console_puts("cat");
    console_set_color(COLOR_WHITE, COLOR_BLACK);
    console_puts(" <file>    - Show file contents\n");

    console_puts("  ");
    console_set_color(COLOR_GREEN, COLOR_BLACK);
    console_puts("vi");
    console_set_color(COLOR_WHITE, COLOR_BLACK);
    console_puts(" <file>     - Edit file\n");

    console_set_color(COLOR_AMBER, COLOR_BLACK);
    console_puts(" System:\n");
    console_set_color(COLOR_WHITE, COLOR_BLACK);

    console_puts("  ");
    console_set_color(COLOR_GREEN, COLOR_BLACK);
    console_puts("help");
    console_set_color(COLOR_WHITE, COLOR_BLACK);
    console_puts("           - Show this help\n");

    console_puts("  ");
    console_set_color(COLOR_GREEN, COLOR_BLACK);
    console_puts("clear");
    console_set_color(COLOR_WHITE, COLOR_BLACK);
    console_puts("          - Clear the screen\n");

    console_puts("  ");
    console_set_color(COLOR_GREEN, COLOR_BLACK);
    console_puts("echo");
    console_set_color(COLOR_WHITE, COLOR_BLACK);
    console_puts(" <text>   - Print text\n");

    console_puts("  ");
    console_set_color(COLOR_GREEN, COLOR_BLACK);
    console_puts("version");
    console_set_color(COLOR_WHITE, COLOR_BLACK);
    console_puts("        - Show version\n");

    console_puts("  ");
    console_set_color(COLOR_GREEN, COLOR_BLACK);
    console_puts("mem");
    console_set_color(COLOR_WHITE, COLOR_BLACK);
    console_puts("            - Show memory info\n");

    console_set_color(COLOR_AMBER, COLOR_BLACK);
    console_puts(" Games:\n");
    console_set_color(COLOR_WHITE, COLOR_BLACK);

    console_puts("  ");
    console_set_color(COLOR_GREEN, COLOR_BLACK);
    console_puts("snake");
    console_set_color(COLOR_WHITE, COLOR_BLACK);
    console_puts("          - Play Snake!\n");

    console_puts("  ");
    console_set_color(COLOR_GREEN, COLOR_BLACK);
    console_puts("tetris");
    console_set_color(COLOR_WHITE, COLOR_BLACK);
    console_puts("         - Play Tetris!\n");
}

static void cmd_clear(void) {
    console_clear();
}

static void cmd_echo(int argc, char *argv[]) {
    for (int i = 1; i < argc; i++) {
        console_puts(argv[i]);
        if (i < argc - 1) {
            console_putc(' ');
        }
    }
    console_putc('\n');
}

static void cmd_version(void) {
    console_set_color(COLOR_GREEN, COLOR_BLACK);
    console_puts("VibeOS");
    console_set_color(COLOR_WHITE, COLOR_BLACK);
    console_puts(" v0.1 - aarch64\n");
    console_puts("Built for QEMU virt machine\n");
    console_puts("The vibes are immaculate.\n");
}

static void cmd_memory(void) {
    size_t free_bytes = memory_free();
    size_t used_bytes = memory_used();

    uint32_t free_mb = (uint32_t)(free_bytes / 1024 / 1024);

    console_puts("Memory:\n");
    printf("  Used: %u bytes\n", (uint32_t)used_bytes);
    printf("  Free: %u MB\n", free_mb);
}

// NOTE: uptime command removed - no timer without interrupts

// ============ Filesystem Commands ============

static void cmd_pwd(void) {
    char path[VFS_MAX_PATH];
    vfs_get_cwd_path(path, sizeof(path));
    console_puts(path);
    console_putc('\n');
}

static void cmd_ls(int argc, char *argv[]) {
    vfs_node_t *dir;

    if (argc > 1) {
        dir = vfs_lookup(argv[1]);
        if (!dir) {
            console_set_color(COLOR_RED, COLOR_BLACK);
            console_puts("ls: ");
            console_puts(argv[1]);
            console_puts(": No such file or directory\n");
            console_set_color(COLOR_WHITE, COLOR_BLACK);
            return;
        }
    } else {
        dir = vfs_get_cwd();
    }

    if (!vfs_is_dir(dir)) {
        // It's a file, just print its name
        console_puts(dir->name);
        console_putc('\n');
        return;
    }

    char name[VFS_MAX_NAME];
    uint8_t type;
    int i = 0;

    while (vfs_readdir(dir, i, name, sizeof(name), &type) == 0) {
        if (type == VFS_DIRECTORY) {
            console_set_color(COLOR_CYAN, COLOR_BLACK);
            console_puts(name);
            console_puts("/");
            console_set_color(COLOR_WHITE, COLOR_BLACK);
        } else {
            console_puts(name);
        }
        console_puts("  ");
        i++;
    }

    if (i > 0) {
        console_putc('\n');
    }
}

static void cmd_cd(int argc, char *argv[]) {
    const char *path;

    if (argc < 2) {
        path = "/home/user";  // Default to home
    } else {
        path = argv[1];
    }

    int result = vfs_set_cwd(path);
    if (result == -1) {
        console_set_color(COLOR_RED, COLOR_BLACK);
        console_puts("cd: ");
        console_puts(path);
        console_puts(": No such file or directory\n");
        console_set_color(COLOR_WHITE, COLOR_BLACK);
    } else if (result == -2) {
        console_set_color(COLOR_RED, COLOR_BLACK);
        console_puts("cd: ");
        console_puts(path);
        console_puts(": Not a directory\n");
        console_set_color(COLOR_WHITE, COLOR_BLACK);
    }
}

static void cmd_mkdir(int argc, char *argv[]) {
    if (argc < 2) {
        console_puts("Usage: mkdir <directory>\n");
        return;
    }

    vfs_node_t *dir = vfs_mkdir(argv[1]);
    if (!dir) {
        console_set_color(COLOR_RED, COLOR_BLACK);
        console_puts("mkdir: cannot create directory '");
        console_puts(argv[1]);
        console_puts("'\n");
        console_set_color(COLOR_WHITE, COLOR_BLACK);
    }
}

static void cmd_touch(int argc, char *argv[]) {
    if (argc < 2) {
        console_puts("Usage: touch <file>\n");
        return;
    }

    vfs_node_t *file = vfs_create(argv[1]);
    if (!file) {
        console_set_color(COLOR_RED, COLOR_BLACK);
        console_puts("touch: cannot create file '");
        console_puts(argv[1]);
        console_puts("'\n");
        console_set_color(COLOR_WHITE, COLOR_BLACK);
    }
}

static void cmd_cat(int argc, char *argv[]) {
    if (argc < 2) {
        console_puts("Usage: cat <file>\n");
        return;
    }

    vfs_node_t *file = vfs_lookup(argv[1]);
    if (!file) {
        console_set_color(COLOR_RED, COLOR_BLACK);
        console_puts("cat: ");
        console_puts(argv[1]);
        console_puts(": No such file or directory\n");
        console_set_color(COLOR_WHITE, COLOR_BLACK);
        return;
    }

    if (vfs_is_dir(file)) {
        console_set_color(COLOR_RED, COLOR_BLACK);
        console_puts("cat: ");
        console_puts(argv[1]);
        console_puts(": Is a directory\n");
        console_set_color(COLOR_WHITE, COLOR_BLACK);
        return;
    }

    // Read and print file contents
    char buf[256];
    size_t offset = 0;
    int bytes;

    while ((bytes = vfs_read(file, buf, sizeof(buf) - 1, offset)) > 0) {
        buf[bytes] = '\0';
        console_puts(buf);
        offset += bytes;
    }

    // Add newline if file doesn't end with one
    if (offset > 0) {
        console_putc('\n');
    }
}

static void cmd_vi(int argc, char *argv[]) {
    if (argc < 2) {
        console_puts("Usage: vi <file>\n");
        return;
    }

    vi_edit(argv[1]);
}

static void cmd_snake(void) {
    int score = snake_run();
    printf("Final score: %d\n", score);
}

static void cmd_tetris(void) {
    int score = tetris_run();
    printf("Final score: %d\n", score);
}

// ============ Shell Core ============

// Parse command line into argc/argv
static int parse_command(char *cmd, char *argv[], int max_args) {
    int argc = 0;
    char *p = cmd;

    while (*p && argc < max_args) {
        // Skip whitespace
        while (*p == ' ' || *p == '\t') {
            p++;
        }

        if (*p == '\0') {
            break;
        }

        // Found start of argument
        argv[argc++] = p;

        // Find end of argument
        while (*p && *p != ' ' && *p != '\t') {
            p++;
        }

        // Null-terminate this argument
        if (*p) {
            *p++ = '\0';
        }
    }

    return argc;
}

// Simple string comparison that definitely works
static int str_eq(const char *a, const char *b) {
    while (*a && *b) {
        if (*a != *b) return 0;
        a++;
        b++;
    }
    return (*a == *b);
}

// Handle output redirection: echo foo > file
static int handle_redirect(int argc, char *argv[]) {
    // Look for > in arguments
    for (int i = 1; i < argc - 1; i++) {
        if (str_eq(argv[i], ">")) {
            // argv[i+1] is the filename
            char *filename = argv[i + 1];

            // Create/open file
            vfs_node_t *file = vfs_create(filename);
            if (!file) {
                console_set_color(COLOR_RED, COLOR_BLACK);
                console_puts("Cannot create file: ");
                console_puts(filename);
                console_putc('\n');
                console_set_color(COLOR_WHITE, COLOR_BLACK);
                return -1;
            }

            // Build content from args before >
            char content[512];
            int pos = 0;
            for (int j = 1; j < i && pos < 510; j++) {
                int len = strlen(argv[j]);
                for (int k = 0; k < len && pos < 510; k++) {
                    content[pos++] = argv[j][k];
                }
                if (j < i - 1 && pos < 510) {
                    content[pos++] = ' ';
                }
            }
            content[pos] = '\0';

            // Write to file
            vfs_write(file, content, pos);
            return 1;  // Handled
        }
    }
    return 0;  // No redirect
}

// Execute a command
static void execute_command(char *cmd) {
    char *argv[MAX_ARGS];
    int argc = parse_command(cmd, argv, MAX_ARGS);

    if (argc == 0) {
        return;  // Empty command
    }

    // Check for echo with redirection
    if (str_eq(argv[0], "echo") && argc > 2) {
        int result = handle_redirect(argc, argv);
        if (result != 0) {
            return;  // Handled (success or error)
        }
    }

    // Match commands directly
    if (str_eq(argv[0], "help")) {
        cmd_help();
    } else if (str_eq(argv[0], "clear")) {
        cmd_clear();
    } else if (str_eq(argv[0], "echo")) {
        cmd_echo(argc, argv);
    } else if (str_eq(argv[0], "version")) {
        cmd_version();
    } else if (str_eq(argv[0], "mem")) {
        cmd_memory();
    } else if (str_eq(argv[0], "pwd")) {
        cmd_pwd();
    } else if (str_eq(argv[0], "ls")) {
        cmd_ls(argc, argv);
    } else if (str_eq(argv[0], "cd")) {
        cmd_cd(argc, argv);
    } else if (str_eq(argv[0], "mkdir")) {
        cmd_mkdir(argc, argv);
    } else if (str_eq(argv[0], "touch")) {
        cmd_touch(argc, argv);
    } else if (str_eq(argv[0], "cat")) {
        cmd_cat(argc, argv);
    } else if (str_eq(argv[0], "vi")) {
        cmd_vi(argc, argv);
    } else if (str_eq(argv[0], "snake")) {
        cmd_snake();
    } else if (str_eq(argv[0], "tetris")) {
        cmd_tetris();
    } else {
        // Try to execute as a program
        // First check if it's a path
        char path[256];
        if (argv[0][0] == '/' || argv[0][0] == '.') {
            // Absolute or relative path
            strncpy(path, argv[0], sizeof(path) - 1);
            path[sizeof(path) - 1] = '\0';
        } else {
            // Look in /bin
            strcpy(path, "/bin/");
            strncpy(path + 5, argv[0], sizeof(path) - 6);
            path[sizeof(path) - 1] = '\0';
        }

        // Try to execute
        vfs_node_t *prog = vfs_lookup(path);
        if (prog && !vfs_is_dir(prog)) {
            // Replace argv[0] with full path for the program
            argv[0] = path;
            int result = process_exec_args(path, argc, argv);
            if (result < 0) {
                console_set_color(COLOR_RED, COLOR_BLACK);
                console_puts("Failed to execute: ");
                console_puts(path);
                console_putc('\n');
                console_set_color(COLOR_WHITE, COLOR_BLACK);
            }
        } else {
            // Command not found
            console_set_color(COLOR_RED, COLOR_BLACK);
            console_puts("Unknown command: ");
            console_set_color(COLOR_WHITE, COLOR_BLACK);
            console_puts(argv[0]);
            console_puts("\nType 'help' for available commands.\n");
        }
    }
}

// Print the shell prompt
static void print_prompt(void) {
    console_set_color(COLOR_GREEN, COLOR_BLACK);
    console_puts("vibe");
    console_set_color(COLOR_WHITE, COLOR_BLACK);
    console_puts("> ");
}

void shell_init(void) {
    cmd_pos = 0;
    cmd_buffer[0] = '\0';
}

void shell_run(void) {
    shell_init();

    console_puts("\n");
    console_set_color(COLOR_AMBER, COLOR_BLACK);
    console_puts("Welcome to VibeOS Shell!\n");
    console_set_color(COLOR_WHITE, COLOR_BLACK);
    console_puts("Type 'help' for available commands.\n\n");

    print_prompt();

    while (1) {
        int c = keyboard_getc();

        if (c < 0) {
            // No key available, keep polling
            continue;
        }

        if (c == '\n' || c == '\r') {
            // Enter pressed - execute command
            console_putc('\n');
            cmd_buffer[cmd_pos] = '\0';

            if (cmd_pos > 0) {
                execute_command(cmd_buffer);
            }

            // Reset for next command
            cmd_pos = 0;
            cmd_buffer[0] = '\0';
            print_prompt();

        } else if (c == '\b' || c == 127) {
            // Backspace
            if (cmd_pos > 0) {
                cmd_pos--;
                console_putc('\b');
            }

        } else if (c >= 32 && c < 127) {
            // Printable character
            if (cmd_pos < MAX_CMD_LEN - 1) {
                cmd_buffer[cmd_pos++] = (char)c;
                console_putc((char)c);
            }
        }
    }
}
