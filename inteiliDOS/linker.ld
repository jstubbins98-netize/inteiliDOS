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
     * BSS is explicitly cleared by boot.asm, never assumed to be clean RAM. */
    _kernel_data_end = ALIGN(512);

    .bss ALIGN(4K) : {
        _bss_start = .;
        *(COMMON)
        *(.bss*)
        _bss_end = .;
    }

    /* Symbol marking the end of the kernel image */
    _kernel_end = ALIGN(4K);
    ASSERT(_kernel_end <= 0x500000, "Kernel overlaps LaunchPad staging at 0x500000")

    /DISCARD/ : {
        *(.comment)
        *(.note*)
        *(.eh_frame*)
    }
}
