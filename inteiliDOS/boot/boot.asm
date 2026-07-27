; =============================================================================
; inteiliDOS -- boot/boot.asm
; Multiboot1-compliant entry point for x86 (32-bit protected mode kernel).
;
; _start is placed at KERN_DEST (0x100000) so BOTH paths work:
;   • GRUB/Multiboot: GRUB reads the ELF entry point and jumps to _start.
;     It finds the Multiboot header by scanning the first 8 KB for the magic.
;   • Custom MBR:     MBR copies kernel to 0x100000 then does
;                       xor eax,eax ; xor ebx,ebx ; jmp 0x100000
;                     which lands here and jumps over the header to _kernel_entry.
; =============================================================================

BITS 32

MULTIBOOT_MAGIC     equ 0x1BADB002
MULTIBOOT_FLAGS     equ 0x00000003   ; bit 0: 4 KB align  bit 1: memory map
MULTIBOOT_CHECKSUM  equ -(MULTIBOOT_MAGIC + MULTIBOOT_FLAGS)

; ── Stack (16 KB) ─────────────────────────────────────────────────────────────
section .bss
align 16
stack_bottom:
    resb 16384
stack_top:

; ── Entry point ───────────────────────────────────────────────────────────────
section .text
global _start
extern kernel_main

_start:
    ; Jump over the inline Multiboot1 header so that the MBR can call
    ; 0x100000 directly.  GRUB finds the header by scanning for the magic
    ; word — no dedicated section is required.
    jmp  _kernel_entry

; ── Multiboot1 header ─────────────────────────────────────────────────────────
; Must be 4-byte aligned and within the first 8 KB of the kernel image.
align 4
    dd MULTIBOOT_MAGIC
    dd MULTIBOOT_FLAGS
    dd MULTIBOOT_CHECKSUM

; ── Kernel bootstrap ──────────────────────────────────────────────────────────
_kernel_entry:
    mov  esp, stack_top

    ; Forward Multiboot info to kernel_main(magic, info_phys).
    ;   GRUB sets: eax = 0x2BADB002, ebx = Multiboot info ptr
    ;   MBR sets:  eax = 0,          ebx = 0
    push ebx        ; second arg: multiboot_info_t physical pointer
    push eax        ; first  arg: magic (0x2BADB002 or 0)

    push 0
    popf            ; zero EFLAGS

    call kernel_main

.halt:
    cli
    hlt
    jmp  .halt
