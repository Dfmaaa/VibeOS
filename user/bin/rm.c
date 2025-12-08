/*
 * rm - remove files
 */

#include "../lib/vibe.h"

int main(kapi_t *k, int argc, char **argv) {
    if (argc < 2) {
        k->puts("Usage: rm <file> [...]\n");
        return 1;
    }

    int status = 0;

    for (int i = 1; i < argc; i++) {
        if (k->delete(argv[i]) < 0) {
            k->set_color(COLOR_RED, COLOR_BLACK);
            k->puts("rm: cannot remove '");
            k->puts(argv[i]);
            k->puts("'\n");
            k->set_color(COLOR_WHITE, COLOR_BLACK);
            status = 1;
        }
    }

    return status;
}
