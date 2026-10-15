; Test-only Multiboot entry for the actual ATA/ATAPI drivers.
BITS 32
GLOBAL _start
EXTERN test_main
SECTION .multiboot
ALIGN 4
dd 0x1BADB002, 3, -(0x1BADB002 + 3)
SECTION .text
_start:
    cli
    lgdt [gdt_pointer]
    jmp 0x08:.flat
.flat:
    mov ax, 0x10
    mov ds, ax
    mov es, ax
    mov ss, ax
    mov fs, ax
    mov gs, ax
    mov esp, stack_top
    cld
    ; Both storage drivers poll. No test interrupt handlers are required.
    mov al, 0xFF
    out 0x21, al
    out 0xA1, al
    call test_main
.halt:
    hlt
    jmp .halt
SECTION .rodata
gdt: dq 0, 0x00CF9A000000FFFF, 0x00CF92000000FFFF
gdt_pointer:
    dw 23
    dd gdt
SECTION .bss
ALIGN 16
stack: resb 32768
stack_top:
SECTION .note.GNU-stack noalloc noexec nowrite progbits
