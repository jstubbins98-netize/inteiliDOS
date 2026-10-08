/* inteilidOS -- linker.ld
 * Flat 32-bit kernel linked at 1 MB (0x00100000).
 * GRUB/Multiboot2 loads the kernel here.
 */

OUTPUT_FORMAT(elf32-i386)
OUTPUT_ARCH(i386)
ENTRY(_start)

SECTIONS
{
    /* Load address: 1 MB */
    . = 0x00100000;

    /* .multiboot is now embedded inline in .text (see boot/boot.asm).
     * .text starts at exactly 0x100000 so the MBR can jump straight to
     * _start without knowing its ELF offset. */
    .text ALIGN(4K) : {
        *(.text*)
    }

    .rodata ALIGN(4K) : {
        *(.rodata*)
    }

    .data ALIGN(4K) : {
        *(.data*)
    }

    /* End of sections that must be loaded from disk.
     * BSS is zero-initialised; on a fresh QEMU image the RAM is already
     * zero, so we do not need to store or load it. */
    _kernel_data_end = ALIGN(512);

    .bss ALIGN(4K) : {
        *(COMMON)
        *(.bss*)
    }

    /* Symbol marking the end of the kernel image */
    _kernel_end = ALIGN(4K);

    /DISCARD/ : {
        *(.comment)
        *(.note*)
        *(.eh_frame*)
    }
}
