/*
 * vibesh - VibeOS Shell
 *
 * A userspace shell for VibeOS. Reads commands, parses them,
 * and either handles builtins or executes programs from /bin.
 *
 * Builtins:
 *   cd <dir>    - Change directory (must be builtin)
 *   exit        - Exit shell
 *   help        - Show help
 *
 * Everything else is looked up in /bin and executed.
 */

#include "../lib/vibe.h"

// Shell limits
#define CMD_MAX     256
#define MAX_ARGS    16
#define PATH_MAX    256

// Global API pointer
static kapi_t *k;

// Command buffer
static char cmd_buf[CMD_MAX];
static int cmd_pos;

// Parse command line into argc/argv
// Modifies cmd in place (inserts null terminators)
static int parse_command(char *cmd, char *argv[], int max_args) {
    int argc = 0;
    char *p = cmd;

    while (*p && argc < max_args) {
        // Skip whitespace
        while (*p == ' ' || *p == '\t') {
            p++;
        }

        if (*p == '\0') break;

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

// Print the shell prompt
static void print_prompt(void) {
    char cwd[PATH_MAX];
    k->get_cwd(cwd, PATH_MAX);

    k->set_color(COLOR_CYAN, COLOR_BLACK);
    k->puts(cwd);
    k->set_color(COLOR_WHITE, COLOR_BLACK);
    k->puts(" $ ");
}

// Builtin: cd
static int builtin_cd(int argc, char *argv[]) {
    if (argc < 2) {
        // No argument - go to /home/user
        if (k->set_cwd("/home/user") < 0) {
            k->set_color(COLOR_RED, COLOR_BLACK);
            k->puts("cd: failed\n");
            k->set_color(COLOR_WHITE, COLOR_BLACK);
            return 1;
        }
    } else {
        if (k->set_cwd(argv[1]) < 0) {
            k->set_color(COLOR_RED, COLOR_BLACK);
            k->puts("cd: ");
            k->puts(argv[1]);
            k->puts(": No such directory\n");
            k->set_color(COLOR_WHITE, COLOR_BLACK);
            return 1;
        }
    }
    return 0;
}

// Builtin: help
static void builtin_help(void) {
    k->puts("vibesh - VibeOS Shell\n\n");
    k->puts("Builtins:\n");
    k->puts("  cd <dir>    Change directory\n");
    k->puts("  exit        Exit shell\n");
    k->puts("  help        Show this help\n");
    k->puts("\nExternal commands in /bin:\n");
    k->puts("  echo, ls, cat, pwd, mkdir, touch, rm, vi\n");
}

// Try to execute an external command
static int exec_external(int argc, char *argv[]) {
    // Build path to binary
    char path[PATH_MAX];

    // If it starts with / or ., use as-is
    if (argv[0][0] == '/' || argv[0][0] == '.') {
        strncpy_safe(path, argv[0], PATH_MAX);
    } else {
        // Look in /bin
        strcpy(path, "/bin/");
        strcat(path, argv[0]);
    }

    // Check if the file exists
    void *file = k->open(path);
    if (!file) {
        k->set_color(COLOR_RED, COLOR_BLACK);
        k->puts(argv[0]);
        k->puts(": command not found\n");
        k->set_color(COLOR_WHITE, COLOR_BLACK);
        return 127;
    }

    // Execute it with arguments
    int result = k->exec_args(path, argc, argv);
    return result;
}

// Execute a command
static int execute_command(char *cmd) {
    char *argv[MAX_ARGS];
    int argc = parse_command(cmd, argv, MAX_ARGS);

    if (argc == 0) {
        return 0;  // Empty command
    }

    // Check for builtins
    if (strcmp(argv[0], "cd") == 0) {
        return builtin_cd(argc, argv);
    }

    if (strcmp(argv[0], "exit") == 0) {
        return -1;  // Signal to exit shell
    }

    if (strcmp(argv[0], "help") == 0) {
        builtin_help();
        return 0;
    }

    // Not a builtin - try external command
    return exec_external(argc, argv);
}

// Read a line of input
static int read_line(void) {
    cmd_pos = 0;
    cmd_buf[0] = '\0';

    while (1) {
        int c = k->getc();

        if (c < 0) {
            // No input, yield to other processes
            k->yield();
            continue;
        }

        if (c == '\r' || c == '\n') {
            // Enter pressed
            k->putc('\n');
            cmd_buf[cmd_pos] = '\0';
            return 0;
        }

        if (c == '\b' || c == 127) {
            // Backspace
            if (cmd_pos > 0) {
                cmd_pos--;
                k->putc('\b');
                k->putc(' ');
                k->putc('\b');
            }
            continue;
        }

        if (c == 27) {
            // Escape - could handle arrow keys etc. later
            continue;
        }

        // Regular character
        if (c >= 32 && c < 127 && cmd_pos < CMD_MAX - 1) {
            cmd_buf[cmd_pos++] = (char)c;
            k->putc((char)c);
        }
    }
}

int main(kapi_t *api, int argc, char **argv) {
    k = api;

    // Print banner
    k->set_color(COLOR_GREEN, COLOR_BLACK);
    k->puts("vibesh ");
    k->set_color(COLOR_WHITE, COLOR_BLACK);
    k->puts("- VibeOS Shell\n");
    k->puts("Type 'help' for commands.\n\n");

    // Main loop
    while (1) {
        print_prompt();

        if (read_line() < 0) {
            break;  // Error reading
        }

        int result = execute_command(cmd_buf);
        if (result == -1) {
            // Exit command
            break;
        }
    }

    k->puts("Goodbye!\n");
    return 0;
}
