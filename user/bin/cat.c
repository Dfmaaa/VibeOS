/*
 * cat - concatenate and print files
 *
 * Supports multiple files: cat file1 file2
 */

#include "../lib/vibe.h"

int main(kapi_t *k, int argc, char **argv) {
    if (argc < 2) {
        k->puts("Usage: cat <file> [...]\n");
        return 1;
    }

    int status = 0;

    for (int i = 1; i < argc; i++) {
        void *file = k->open(argv[i]);
        if (!file) {
            k->set_color(COLOR_RED, COLOR_BLACK);
            k->puts("cat: ");
            k->puts(argv[i]);
            k->puts(": No such file\n");
            k->set_color(COLOR_WHITE, COLOR_BLACK);
            status = 1;
            continue;
        }

        if (k->is_dir(file)) {
            k->set_color(COLOR_RED, COLOR_BLACK);
            k->puts("cat: ");
            k->puts(argv[i]);
            k->puts(": Is a directory\n");
            k->set_color(COLOR_WHITE, COLOR_BLACK);
            status = 1;
            continue;
        }

        // Read and print file contents
        char buf[256];
        size_t offset = 0;
        int bytes;

        while ((bytes = k->read(file, buf, sizeof(buf) - 1, offset)) > 0) {
            buf[bytes] = '\0';
            k->puts(buf);
            offset += bytes;
        }
    }

    return status;
}
