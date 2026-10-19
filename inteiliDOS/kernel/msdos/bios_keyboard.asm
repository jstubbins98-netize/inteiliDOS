; Compatibility overlay for the bundled 8086tiny BIOS revision 1.61.
; Copyright 2026 inteiliDOS contributors. MIT license, see LICENSE.txt.
;
; Keep bios.h (the upstream ROM) unchanged. This assembles a stream of
; little-endian (guest offset, byte count, payload) records applied at load.
; The hooks' original instructions are checked by tiny.c before patching.
; Regenerate bios_keyboard.h with the commands in README.md.
bits 16
cpu 8086

section .hooks start=0 vstart=0 align=1
dw 0x0332, 3
db 0xe9
dw int7_input - (0x0332 + 3)

dw 0x0658, 6
db 0xe9
dw int9_extended - (0x0658 + 3)
times 3 db 0x90

dw 0x0eac, 5
db 0xe9
dw int16_extended - (0x0eac + 3)
times 2 db 0x90

; The ROM ends at 1EF1h; its RTC scratch data extends through 1F18h.
; 2000h is free, below the guest BIOS stack at F000h.
dw 0x2000, extension_end - int9_extended

section .extension follows=.hooks vstart=0x2000 align=1
int9_extended:
    ; The input overlay supplies the full translated BIOS word, while the
    ; guest's IRQ1 handlers still see the original physical make/break code.
    cmp byte [cs:input_active], 0
    je .hardware
    mov bh, al
    mov ax, [es:0xa6]
    cmp ah, 0
    je .insert
    mov bh, ah
.insert:
    jmp word 0x065e
.hardware:
    ; INT 7h translates SDL F11/F12 to hardware make codes 57h/58h.
    ; INT 9h has already rejected break/modifier codes. Convert only these
    ; two make codes to the BIOS buffer values 8500h/8600h, not 5700h/5800h.
    cmp al, 0x57
    jb .ordinary
    cmp al, 0x58
    ja .ordinary
    add al, 0x2e
.ordinary:
    mov bh, al
    mov al, [es:0xa6]          ; displaced instruction: saved ASCII byte
    jmp word 0x065e           ; resume INT 9h buffer insertion

int16_extended:
    ; Enhanced and legacy services share this BIOS's buffer representation.
    ; Preserve AH=12h (extended shift flags) and all other original services.
    cmp ah, 0x10
    je .read
    cmp ah, 0x11
    je .check
    cmp ah, 0
    je .read
    cmp ah, 0x12
    je .flags
    jmp word 0x0eb1           ; resume original dispatch at AH=01h
.read:
    jmp word 0x0ec1           ; blocking read, removes exactly one entry
.check:
    jmp word 0x0eef           ; peek with ZF, leaves the entry in place
.flags:
    push es
    push bx
    mov bx, 0x40
    mov es, bx
    mov al, [es:0x17]
    mov ah, [cs:extended_flags]
    pop bx
    pop es
    iret

; The host supplies a BIOS scan:ASCII word at 0040:00A6 and a state
; snapshot at F000:2400 (compact modifiers/Caps/state-only, physical scan).
; Replace the original SDL/terminal translator: it synthesized releases
; after every key and never applied the BIOS modified-function-key tables.
; State-only input must update flags without adding any buffer entry.
int7_input:
    push ds
    push es
    push ax
    push bx
    push bp
    mov bx, 0x40
    mov es, bx
    mov bx, [cs:0x2400]
    xor ax, ax
    test bl, 0x02
    jz .no_lshift
    or al, 2
.no_lshift:
    test bl, 0x10
    jz .no_rshift
    or al, 1
.no_rshift:
    test bl, 0x09
    jz .no_ctrl
    or al, 4
.no_ctrl:
    test bl, 0x24
    jz .no_alt
    or al, 8
.no_alt:
    test bl, 0x40
    jz .no_caps
    or al, 0x40
.no_caps:
    ; Keep other lock flags; only CapsLock is currently a native toggle.
    and byte [es:0x17], 0xb0
    or [es:0x17], al
    test bl, 0x01
    jz .no_lctrl
    or ah, 1
.no_lctrl:
    test bl, 0x04
    jz .no_lalt
    or ah, 2
.no_lalt:
    mov al, ah
    and byte [es:0x18], 0xfc
    or [es:0x18], al
    test bl, 0x08
    jz .no_rctrl
    or ah, 4
.no_rctrl:
    test bl, 0x20
    jz .no_ralt
    or ah, 8
.no_ralt:
    mov [cs:extended_flags], ah
    and byte [es:0x96], 0xf3
    and ah, 0x0c
    or [es:0x96], ah
    test bl, 0x80
    jnz .done
    mov al, bh
    cmp al, 0
    jne .inject
    ; Legacy keyboard_inject(char) has no physical position. Recover the
    ; ordinary ASCII position with the upstream ROM's reverse table.
    mov al, [es:0xa6]
    push ds
    push cs
    pop ds
    mov bx, 0x15d9
    xlat
    pop ds
.inject:
    mov byte [cs:input_active], 1
    call word 0x1221          ; original make/previous-break -> guest INT 9h
    mov byte [cs:input_active], 0
.done:
    pop bp
    pop bx
    pop ax
    pop es
    pop ds
    iret
extended_flags: db 0
input_active: db 0
extension_end:

%if extension_end - $$ > 0x400
    %error "Keyboard extension overlaps input mailbox"
%endif
