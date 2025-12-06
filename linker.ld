/*
 * VibeOS Linker Script
 *
 * Memory layout for QEMU virt machine (aarch64)
 * RAM starts at 0x40000000
 */

ENTRY(_start)

SECTIONS
{
    /* QEMU virt machine loads kernel at 0x40000000 */
    . = 0x40000000;

    /* Boot code must come first */
    .text.boot : {
        *(.text.boot)
    }

    /* Rest of code */
    .text : {
        *(.text)
        *(.text.*)
    }

    /* Read-only data */
    .rodata : {
        *(.rodata)
        *(.rodata.*)
    }

    /* Initialized data */
    .data : {
        *(.data)
        *(.data.*)
    }

    /* BSS section (uninitialized data) */
    .bss : {
        _bss_start = .;
        *(.bss)
        *(.bss.*)
        *(COMMON)
        _bss_end = .;
    }

    /* Discard unneeded sections */
    /DISCARD/ : {
        *(.comment)
        *(.note.*)
        *(.eh_frame)
    }
}
