; inteilidOS -- boot/isr_stubs.asm
; ISR and IRQ stub trampolines

BITS 32
section .text

extern isr_common_stub
extern irq_common_stub

; Macro: ISR with no error code (CPU pushes 0)
%macro ISR_NOERR 1
global isr%1
isr%1:
    cli
    push dword 0
    push dword %1
    jmp isr_common_stub
%endmacro

; Macro: ISR with CPU-pushed error code
%macro ISR_ERR 1
global isr%1
isr%1:
    cli
    push dword %1
    jmp isr_common_stub
%endmacro

; Macro: IRQ (remap to INT 32+N)
%macro IRQ 2
global irq%1
irq%1:
    cli
    push dword 0
    push dword %2
    jmp irq_common_stub
%endmacro

; CPU exceptions 0-31
ISR_NOERR  0   ; Divide-by-zero
ISR_NOERR  1   ; Debug
ISR_NOERR  2   ; NMI
ISR_NOERR  3   ; Breakpoint
ISR_NOERR  4   ; Overflow
ISR_NOERR  5   ; Bound range exceeded
ISR_NOERR  6   ; Invalid opcode
ISR_NOERR  7   ; Device not available
ISR_ERR    8   ; Double fault (error code)
ISR_NOERR  9   ; Coprocessor segment overrun
ISR_ERR   10   ; Invalid TSS
ISR_ERR   11   ; Segment not present
ISR_ERR   12   ; Stack-segment fault
ISR_ERR   13   ; General protection fault
ISR_ERR   14   ; Page fault
ISR_NOERR 15
ISR_NOERR 16   ; x87 FP exception
ISR_ERR   17   ; Alignment check
ISR_NOERR 18   ; Machine check
ISR_NOERR 19   ; SIMD FP exception
ISR_NOERR 20
ISR_NOERR 21
ISR_NOERR 22
ISR_NOERR 23
ISR_NOERR 24
ISR_NOERR 25
ISR_NOERR 26
ISR_NOERR 27
ISR_NOERR 28
ISR_NOERR 29
ISR_NOERR 30
ISR_NOERR 31

; IRQs (remapped to 32-47)
IRQ  0, 32   ; PIT Timer
IRQ  1, 33   ; Keyboard
IRQ  2, 34
IRQ  3, 35
IRQ  4, 36
IRQ  5, 37
IRQ  6, 38
IRQ  7, 39
IRQ  8, 40   ; RTC
IRQ  9, 41
IRQ 10, 42
IRQ 11, 43
IRQ 12, 44   ; PS/2 Mouse
IRQ 13, 45
IRQ 14, 46   ; Primary ATA
IRQ 15, 47   ; Secondary ATA

; ------------------------------------------------------------------
; Common ISR stub: save registers, call C handler, restore, iret
; ------------------------------------------------------------------
isr_common_stub:
    pusha
    push ds
    push es
    push fs
    push gs
    mov ax, 0x10       ; kernel data segment
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    ; ── Force IRET to use our kernel CS (selector 0x08) ───────────────────
    ; GRUB may enter the kernel with a CS selector different from 0x08 (e.g.
    ; 0x10 in GRUB's internal GDT).  The CPU saves that selector on the iret
    ; frame.  When IRET restores CS from our GDT, 0x10 is the *data* segment
    ; → #GP.  Patch the saved CS to 0x08 (kcode) so IRET always reloads our
    ; flat ring-0 code segment, regardless of what the bootloader used.
    ;
    ; Stack layout here (after pusha + push ds/es/fs/gs, before push esp):
    ;   [ESP+ 0] GS     [ESP+16] EDI … [ESP+44] EAX
    ;   [ESP+48] int_no [ESP+52] err   [ESP+56] EIP  [ESP+60] CS  [ESP+64] EFLAGS
    mov  dword [esp + 60], 0x08
    ; ─────────────────────────────────────────────────────────────────────
    push esp           ; pointer to registers_t
    call isr_dispatch
    add esp, 4
    pop gs
    pop fs
    pop es
    pop ds
    popa
    add esp, 8         ; pop int_no and err_code
    iret

extern isr_dispatch

; ------------------------------------------------------------------
; Common IRQ stub
; ------------------------------------------------------------------
irq_common_stub:
    pusha
    push ds
    push es
    push fs
    push gs
    mov ax, 0x10
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    ; Same CS patch as isr_common_stub — IRQ iret frames need the same fix.
    mov  dword [esp + 60], 0x08
    push esp
    call irq_dispatch
    add esp, 4
    pop gs
    pop fs
    pop es
    pop ds
    popa
    add esp, 8
    iret

extern irq_dispatch
