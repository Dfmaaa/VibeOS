/*
 * Explode - Test program that causes a crash
 */

#include "../lib/vibe.h"

int main(kapi_t *k, int argc, char **argv) {
    (void)argc; (void)argv;

    // Countdown from 3000ms
    for (int ms = 3000; ms > 0; ms -= 100) {
        vibe_print_int(k, ms);
        vibe_puts(k, " ms\n");
        k->sleep_ms(100);
    }

    vibe_puts(k, "kernel panic\n");

    // Crash by dereferencing null pointer
    volatile int *kaboom = (volatile int *)0;
    *kaboom = 0xDEAD;

    return 0;
}
