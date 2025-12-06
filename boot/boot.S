/*
 * VibeOS - aarch64 Bootloader
 *
 * Entry point for the kernel. Sets up the stack and jumps to C code.
 * Targets QEMU virt machine.
 */

.section ".text.boot"

.global _start

_start:
    // Get the CPU ID - only CPU 0 should initialize
    mrs     x0, mpidr_el1
    and     x0, x0, #0xFF
    cbz     x0, primary_cpu

    // Secondary CPUs go to sleep
secondary_cpu:
    wfe
    b       secondary_cpu

primary_cpu:
    // Set up the stack pointer (stack grows downward)
    // Place stack at 2MB mark
    ldr     x0, =_stack_top
    mov     sp, x0

    // Clear BSS section
    ldr     x0, =_bss_start
    ldr     x1, =_bss_end
clear_bss:
    cmp     x0, x1
    b.ge    bss_done
    str     xzr, [x0], #8
    b       clear_bss
bss_done:

    // Jump to kernel main
    bl      kernel_main

    // If kernel_main returns, halt
halt:
    wfe
    b       halt

.section ".data"

.section ".bss"
.align 16
_stack_bottom:
    .skip 0x10000  // 64KB stack
_stack_top:
