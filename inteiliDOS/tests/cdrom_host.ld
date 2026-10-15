OUTPUT_FORMAT(elf32-i386)
OUTPUT_ARCH(i386)
ENTRY(_start)
PHDRS {
    text PT_LOAD FLAGS(5);
    data PT_LOAD FLAGS(6);
}
SECTIONS {
    . = 0x00100000;
    .text : { KEEP(*(.multiboot)) *(.text*) *(.rodata*) } :text
    . = ALIGN(4096);
    .data : { *(.data*) } :data
    .bss : { *(COMMON) *(.bss*) } :data
    /DISCARD/ : { *(.eh_frame*) *(.comment) *(.note*) }
}
