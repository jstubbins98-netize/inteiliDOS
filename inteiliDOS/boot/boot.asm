; =============================================================================
; inteiliDOS -- boot/boot.asm
; Multiboot1-compliant entry point for x86 (32-bit protected mode kernel).
;
; _start is placed at KERN_DEST (0x100000) so BOTH paths work:
;   • GRUB/Multiboot: GRUB reads the ELF entry point and jumps to _start.
;     It finds the Multiboot header by scanning the first 8 KB for the magic.
;   • Installed VBR: streams kernel to 0x100000 and supplies a Multiboot
;     E820 memory map. Older raw loaders may still pass zero EAX/EBX.
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
extern _bss_start, _bss_end

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
;
; Dead-reckoning VGA diagnostics — written directly to the VGA frame buffer
; before ANY driver is initialised.  Attribute 0x0F = bright white on black.
; vga_init() overwrites these with spaces, so a clean boot shows nothing here.
;
; Character map (visible against the BIOS blue background if execution stops):
;   col 0  '1'  = _kernel_entry reached
;   col 1  '2'  = stack pointer set
;   col 2  '3'  = about to call kernel_main
;   col 3  '4'  = kernel_main entered  (written in kernel.c before vga_init)
;
_kernel_entry:
    ; Firmware does NOT guarantee zero RAM after setup/warm/cold boot.
    ; Clear BSS before installing/using its stack, preserving Multiboot ABI.
    cli
    cld
    mov edx,eax
    xor eax,eax
    mov edi,_bss_start
    mov ecx,_bss_end
    sub ecx,edi
    rep stosb
    mov eax,edx
    ; ── Dead-reckoning VGA diagnostics ────────────────────────────────────
    ; Written directly to the VGA frame buffer before ANY driver initialises.
    ; Attribute byte 0x0F = bright white on black, visible against the BIOS
    ; blue background.  vga_init() overwrites the screen with spaces, so a
    ; clean boot shows nothing here.
    ;
    ;  VGA word layout: high byte = attribute, low byte = ASCII character.
    ;  Position (row=0, col=N) is at physical 0xB8000 + N*2.
    ;
    ;  col 0  '1'  = _kernel_entry reached
    ;  col 1  '2'  = stack pointer set
    ;  col 2  '3'  = about to call kernel_main
    ;  col 3  '4'  = kernel_main entered  (written in kernel.c)

    mov  word [0xB8000], 0x0F31     ; '1' bright-white — entry reached

    mov  esp, stack_top

    mov  word [0xB8002], 0x0F32     ; '2' — stack pointer set

    ; Forward Multiboot info to kernel_main(magic, info_phys).
    ;   GRUB sets: eax = 0x2BADB002, ebx = Multiboot info ptr
    ;   VBR sets:  eax = 0x2BADB002, ebx = BIOS map at 0x5000
    ;   Old raw loaders may pass eax = ebx = 0 (heap-only native operation).
    push ebx        ; second arg: multiboot_info_t physical pointer
    push eax        ; first  arg: magic (0x2BADB002 or 0)

    push 0
    popf            ; zero EFLAGS

    mov  word [0xB8004], 0x0F33     ; '3' — about to call kernel_main

    call kernel_main

.halt:
    cli
    hlt
    jmp  .halt
