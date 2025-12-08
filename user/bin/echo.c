/*
 * echo - print arguments
 *
 * Supports output redirection: echo hello > file.txt
 */

#include "../lib/vibe.h"

int main(kapi_t *k, int argc, char **argv) {
    // Check for output redirection
    int redir_idx = -1;
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], ">") == 0 && i + 1 < argc) {
            redir_idx = i;
            break;
        }
    }

    if (redir_idx > 0) {
        // Redirect to file
        char *filename = argv[redir_idx + 1];
        void *file = k->create(filename);
        if (!file) {
            k->set_color(COLOR_RED, COLOR_BLACK);
            k->puts("echo: cannot create ");
            k->puts(filename);
            k->putc('\n');
            k->set_color(COLOR_WHITE, COLOR_BLACK);
            return 1;
        }

        // Build content from args before >
        char content[512];
        int pos = 0;
        for (int i = 1; i < redir_idx && pos < 510; i++) {
            int len = strlen(argv[i]);
            for (int j = 0; j < len && pos < 510; j++) {
                content[pos++] = argv[i][j];
            }
            if (i < redir_idx - 1 && pos < 510) {
                content[pos++] = ' ';
            }
        }
        content[pos++] = '\n';
        content[pos] = '\0';

        k->write(file, content, pos);
    } else {
        // Print to console
        for (int i = 1; i < argc; i++) {
            k->puts(argv[i]);
            if (i < argc - 1) {
                k->putc(' ');
            }
        }
        k->putc('\n');
    }

    return 0;
}
