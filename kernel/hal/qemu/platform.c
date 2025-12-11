/*
 * QEMU virt machine Platform Info
 */

#include "../hal.h"

const char *hal_platform_name(void) {
    return "QEMU virt (aarch64)";
}

void hal_wfi(void) {
    asm volatile("wfi");
}
