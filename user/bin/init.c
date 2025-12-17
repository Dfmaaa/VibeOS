/*
 * init - VibeOS init system
 *
 * Reads /etc/init.conf and spawns listed programs.
 * First userspace process (PID 1).
 */

#include "../lib/vibe.h"

static kapi_t *api;

// Helper: check if char is whitespace
static int is_space(char c) {
    return c == ' ' || c == '\t' || c == '\r' || c == '\n';
}

// Helper: read a line from file, returns 1 if line read, 0 if EOF
static int read_line(void *file, char *buf, int max, size_t *offset) {
    int i = 0;
    char c;
    int got_data = 0;

    while (i < max - 1) {
        int bytes = api->read(file, &c, 1, *offset);
        if (bytes <= 0) break;
        got_data = 1;
        (*offset)++;

        if (c == '\n') break;
        if (c != '\r') buf[i++] = c;
    }
    buf[i] = '\0';
    return got_data;
}

// Helper: skip leading whitespace, return pointer to first non-space
static char *skip_space(char *s) {
    while (*s && is_space(*s)) s++;
    return s;
}

// Helper: trim trailing whitespace in place
static void trim_end(char *s) {
    int len = 0;
    while (s[len]) len++;
    while (len > 0 && is_space(s[len - 1])) {
        s[--len] = '\0';
    }
}

int main(kapi_t *k, int argc, char **argv) {
    (void)argc;
    (void)argv;
    api = k;

    api->puts("init: starting\n");

    void *conf = api->open("/etc/init.conf");
    int spawned = 0;

    if (!conf) {
        api->puts("init: /etc/init.conf not found, starting /bin/vibesh\n");
        api->spawn("/bin/vibesh");
        spawned = 1;
    } else {
        char line[256];
        size_t offset = 0;

        while (read_line(conf, line, sizeof(line), &offset)) {
            // Skip leading whitespace
            char *p = skip_space(line);

            // Skip empty lines
            if (*p == '\0') continue;

            // Skip comments
            if (*p == '#') continue;

            // Trim trailing whitespace
            trim_end(p);

            // Skip if empty after trimming
            if (*p == '\0') continue;

            // Spawn the program
            api->puts("init: spawning ");
            api->puts(p);
            api->puts("\n");

            int pid = api->spawn(p);
            if (pid > 0) {
                spawned++;
            } else {
                api->puts("init: failed to spawn ");
                api->puts(p);
                api->puts("\n");
            }
        }

        api->close(conf);

        if (spawned == 0) {
            api->puts("init: no programs in config, starting /bin/vibesh\n");
            api->spawn("/bin/vibesh");
            spawned = 1;
        }
    }

    api->puts("init: ");
    api->print_int(spawned);
    api->puts(" program(s) started, entering idle loop\n");

    // Loop forever - init should never exit
    while (1) {
        api->yield();
    }

    return 0;
}
