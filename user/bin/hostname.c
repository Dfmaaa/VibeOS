/*
 * hostname - print system hostname
 *
 * VibeOS hostname is "vibeos"
 */

#include "../lib/vibe.h"

int main(kapi_t *k, int argc, char **argv) {
    (void)argc;
    (void)argv;

    if (k->stdio_puts) {
        k->stdio_puts("vibeos\n");
    } else {
        k->puts("vibeos\n");
    }

    return 0;
}
