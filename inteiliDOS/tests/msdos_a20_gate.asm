; Original fixture: POST must not leave A20 enabled for DOS unpackers.
; Unlike a test which starts by disabling A20, this detects bad boot state.
bits 16
cpu 486
org 0x100
    push cs
    pop ds
    mov ax,0x2402
    int 0x15
    jc fail
    test ax,ax
    jnz fail
    call alias_check
    ; Enabling A20 through BIOS and both AT hardware interfaces must
    ; remain possible after POST (protected-mode games need this).
    mov ax,0x2401
    int 0x15
    jc fail
    mov ax,0x2402
    int 0x15
    cmp ax,1
    jne fail
    call distinct_check
    mov dx,0x92
    xor al,al
    out dx,al
    call alias_check
    mov al,2
    out dx,al
    call distinct_check
    mov dx,0x64
    mov al,0xd1
    out dx,al
    mov dx,0x60
    mov al,1
    out dx,al
    call alias_check
    mov dx,0x64
    mov al,0xdf
    out dx,al
    call distinct_check
    mov ax,0x2400
    int 0x15
    jc fail
    call alias_check
    mov dx,success
    jmp print

alias_check:
    mov ax,1
    jmp check
distinct_check:
    xor ax,ax
check:
    ; No persistent memory edits: preserve one byte in low RAM and HMA.
    ; IF is saved across CLI so a timer cannot alter the scratch region.
    pushf
    cli
    push ds
    push es
    push bx
    push dx
    xor bx,bx
    mov es,bx
    mov dl,[es:0x700]
    mov bx,0xffff
    mov ds,bx
    mov dh,[0x710]
    mov byte [es:0x700],0x35
    mov byte [0x710],0x6a
    cmp byte [es:0x700],0x6a
    sete bl
    mov [0x710],dh
    mov [es:0x700],dl
    cmp bl,al
    pop dx
    pop bx
    pop es
    pop ds
    jne check_fail
    popf
    ret
check_fail:
    popf
fail:
    mov ax,0x2400
    int 0x15
    push cs
    pop ds
    mov dx,failure
print:
    mov ah,9
    int 0x21
    mov ax,0x4c00
    int 0x21
success db 'A20 BOOT AND TOGGLE PASS',13,10,'$'
failure db 'A20 BOOT AND TOGGLE FAILED',13,10,'$'
