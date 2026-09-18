; inteilidOS -- boot/gdt_flush.asm
; Helper to load GDT and flush segment registers

BITS 32
section .text

global gdt_flush
gdt_flush:
    mov  eax, [esp+4]
    lgdt [eax]
    mov  ax, 0x10           ; kernel data selector
    mov  ds, ax
    mov  es, ax
    mov  fs, ax
    mov  gs, ax
    mov  ss, ax
    ; CS reload (far jump) intentionally omitted.
    ; The MBR and kernel both use CS=0x08 as an identical flat 4 GB ring-0
    ; code segment (base=0, limit=4 GB, DPL=0).  The CPU's cached CS
    ; descriptor is already correct, so a far jump would be a no-op —
    ; and the direct "jmp 0x08:label" encoding caused a triple-fault in
    ; QEMU's i386 emulation (confirmed by step-by-step VGA diagnostics).
    ret

global tss_flush
tss_flush:
    mov  ax, 0x2B           ; TSS selector: index 5 in GDT
    ltr  ax
    ret
