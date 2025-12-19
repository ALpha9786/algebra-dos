/* linker.ld - Linker script for Algebra OS */

ENTRY(_start)

SECTIONS
{
    /* Kernel starts at 1MB */
    . = 1M;

    /* Multiboot header must be in first 8KB */
    .multiboot ALIGN(4K) : {
        *(.multiboot)
    }

    /* Boot code section */
    .text.boot ALIGN(4K) : {
        *(.text.boot)
    }

    /* Main code section */
    .text ALIGN(4K) : {
        *(.text)
    }

    /* Read-only data */
    .rodata ALIGN(4K) : {
        *(.rodata)
    }

    /* Initialized data */
    .data ALIGN(4K) : {
        *(.data)
    }

    /* Uninitialized data */
    .bss ALIGN(4K) : {
        *(COMMON)
        *(.bss)
    }
}
