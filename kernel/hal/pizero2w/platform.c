/*
 * Raspberry Pi Zero 2W Platform Info
 */

#include "../hal.h"

const char *hal_platform_name(void) {
    return "Raspberry Pi Zero 2W";
}

void hal_wfi(void) {
    asm volatile("wfi");
}
