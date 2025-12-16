/*
 * hello.c - Test program for TCC on VibeOS
 *
 * Compile with:
 *   tcc hello.c -o hello
 *
 * Then run:
 *   ./hello
 */

#include <vibe.h>

int main(kapi_t *kapi, int argc, char **argv) {
    kapi->puts("Hello from TCC!\n");

    if (argc > 1) {
        kapi->puts("Arguments: ");
        for (int i = 1; i < argc; i++) {
            kapi->puts(argv[i]);
            kapi->putc(' ');
        }
        kapi->putc('\n');
    }

    return 0;
}
