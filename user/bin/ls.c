/*
 * ls - list directory contents
 *
 * Uses the proper readdir API instead of accessing VFS internals.
 */

#include "../lib/vibe.h"

int main(kapi_t *k, int argc, char **argv) {
    const char *path = ".";

    if (argc > 1) {
        path = argv[1];
    }

    void *dir = k->open(path);
    if (!dir) {
        k->set_color(COLOR_RED, COLOR_BLACK);
        k->puts("ls: ");
        k->puts(path);
        k->puts(": No such file or directory\n");
        k->set_color(COLOR_WHITE, COLOR_BLACK);
        return 1;
    }

    if (!k->is_dir(dir)) {
        // It's a file, just print the name
        k->puts(path);
        k->putc('\n');
        return 0;
    }

    // List directory contents using readdir
    char name[256];
    uint8_t type;
    int index = 0;

    while (k->readdir(dir, index, name, sizeof(name), &type) >= 0) {
        if (type == 2) {
            // Directory
            k->set_color(COLOR_CYAN, COLOR_BLACK);
            k->puts(name);
            k->putc('/');
        } else {
            // File
            k->set_color(COLOR_WHITE, COLOR_BLACK);
            k->puts(name);
        }
        k->putc('\n');
        index++;
    }

    k->set_color(COLOR_WHITE, COLOR_BLACK);
    return 0;
}
