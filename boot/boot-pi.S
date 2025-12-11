/*
 * VibeOS - Raspberry Pi Zero 2W Bootloader
 *
 * Entry point for Pi Zero 2W (BCM2710/Cortex-A53).
 * GPU loads kernel8.img to 0x80000 and starts CPU at EL2.
 *
 * Memory layout:
 *   0x00000000 - 0x0007FFFF: Reserved (GPU, bootloader)
 *   0x00080000 - ...       : Kernel code (.text)
 *   Stack at end of 512MB RAM (0x1F000000)
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
    // Check current exception level
    mrs     x0, CurrentEL
    lsr     x0, x0, #2          // CurrentEL is in bits [3:2]

    cmp     x0, #3
    b.eq    drop_from_el3
    cmp     x0, #2
    b.eq    drop_from_el2
    cmp     x0, #1
    b.eq    at_el1
    // Unknown EL, hang
    b       secondary_cpu

drop_from_el3:
    // We're at EL3 - drop to EL1 (skip EL2)

    // Configure SCR_EL3 (Secure Configuration Register)
    mov     x0, #0x430          // RW=1, NS=0 (secure), no HCE
    msr     scr_el3, x0
    isb

    // Initialize SCTLR_EL1 to safe defaults
    mrs     x0, sctlr_el1
    bic     x0, x0, #(1 << 0)   // Clear M bit (MMU off)
    bic     x0, x0, #(1 << 2)   // Clear C bit (data cache off)
    bic     x0, x0, #(1 << 12)  // Clear I bit (instruction cache off)
    msr     sctlr_el1, x0
    isb

    // Set up SPSR for EL1h
    mov     x0, #0x3c5          // DAIF masked, EL1h
    msr     spsr_el3, x0

    // Set return address to at_el1
    adr     x0, at_el1
    msr     elr_el3, x0

    eret

drop_from_el2:
    // We're at EL2, need to drop to EL1
    // Pi typically starts at EL2

    // Enable AArch64 for EL1
    mov     x0, #(1 << 31)      // RW=1: EL1 is AArch64
    msr     hcr_el2, x0

    // Initialize SCTLR_EL1 to safe defaults
    mrs     x0, sctlr_el1
    bic     x0, x0, #(1 << 0)   // Clear M bit (MMU off)
    bic     x0, x0, #(1 << 2)   // Clear C bit (data cache off)
    bic     x0, x0, #(1 << 12)  // Clear I bit (instruction cache off)
    msr     sctlr_el1, x0
    isb

    // Set up SPSR for EL1h
    mov     x0, #0x3c5          // DAIF masked, EL1h
    msr     spsr_el2, x0

    // Set return address to at_el1
    adr     x0, at_el1
    msr     elr_el2, x0

    eret

at_el1:
    // Enable FPU/SIMD (set CPACR_EL1.FPEN = 0b11)
    mov     x0, #(3 << 20)        // FPEN bits [21:20] = 0b11
    msr     cpacr_el1, x0
    isb

    // Set up the stack pointer
    // Pi Zero 2W has 512MB RAM (0x00000000 - 0x20000000)
    // Stack at 0x1F000000 (near end, leaving room at top)
    ldr     x0, =0x1F000000
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

    // Install exception vector table
    ldr     x0, =exception_vectors
    msr     vbar_el1, x0
    isb

    // Initialize HAL serial (for any early debug via GPIO if connected)
    bl      hal_serial_init

    // Jump to kernel main
    bl      kernel_main

    // If kernel_main returns, halt
halt:
    wfe
    b       halt
