/*
 * pwd - print working directory
 */

#include "../lib/vibe.h"

int main(kapi_t *k, int argc, char **argv) {
    (void)argc;
    (void)argv;

    char cwd[256];
    if (k->get_cwd(cwd, sizeof(cwd)) >= 0) {
        k->puts(cwd);
        k->putc('\n');
        return 0;
    }

    k->set_color(COLOR_RED, COLOR_BLACK);
    k->puts("pwd: error getting current directory\n");
    k->set_color(COLOR_WHITE, COLOR_BLACK);
    return 1;
}
