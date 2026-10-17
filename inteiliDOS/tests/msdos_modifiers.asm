; Real DOS COM fixture. Expectations come from the runner's independent
; BIOS matrix, not from the kernel's translation tables.
bits 16
cpu 8086
org 0x100

; Chain a real DOS IRQ1 hook: replacing BIOS translation must not bypass
; applications that inspect physical make/break codes before chaining.
mov ax,0x3509
int 0x21
mov [old_int9],bx
mov [old_int9+2],es
mov dx,int9_hook
mov ax,0x2509
int 0x21

%macro case 5
    mov byte [expected_scan],%5
    mov bx,%1
    mov bp,%2
    mov si,%3
    mov dx,%%prompt
    mov di,%%release
    call check
    jmp %%after
%%prompt: db 'KEY CASE ',%str(%4),'!',13,10,'$'
%%release: db 'RELEASE CASE ',%str(%4),'!',13,10,'$'
%%after:
%endmacro

%macro state 2
    mov bp,%1
    mov dx,%%prompt
    mov ah,9
    int 0x21
%%wait:
    mov ah,0x12
    int 0x16
    cmp ax,bp
    jne %%wait
    mov ah,1
    int 0x16
    jnz fail
    mov dx,%%done
    mov ah,9
    int 0x21
    jmp %%after
%%prompt: db 'STATE CASE ',%str(%2),'!',13,10,'$'
%%done: db 'STATE OK ',%str(%2),'!',13,10,'$'
%%after:
%endmacro

%include CASE_INCLUDE
mov dx,passed
mov ah,9
int 0x21
call restore_int9
mov ax,0x4c00
int 0x21
restore_int9:
push ds
mov dx,[old_int9]
mov ds,[old_int9+2]
mov ax,0x2509
int 0x21
pop ds
ret

check:
    ; Legacy/enhanced peek, repeat peek, consume, and empty semantics.
    mov ax,si
    inc ah
    int 0x16
    jnz fail
    mov ah,9
    int 0x21
.wait:
    mov ax,si
    inc ah
    int 0x16
    jz .wait
    cmp ax,bx
    jne fail
    mov al,[last_make]
    cmp al,[expected_scan]
    jne fail
    mov ax,si
    inc ah
    int 0x16
    jz fail
    cmp ax,bx
    jne fail
    mov ax,si
    int 0x16
    cmp ax,bx
    jne fail
    mov ah,2
    int 0x16
    xor ah,ah
    push bp
    and bp,0xff
    cmp ax,bp
    pop bp
    jne fail
    mov ah,0x12
    int 0x16
    cmp ax,bp
    jne fail
    ; The runner holds the chord until this acknowledgement, so state
    ; checks cannot accidentally pass after an automatic QEMU key release.
    mov dx,di
    mov ah,9
    int 0x21
.release:
    mov ah,2
    int 0x16
    test al,0x0f
    jnz .release
    mov ah,0x12
    int 0x16
    test ax,0x0f0f
    jnz fail
    mov ax,si
    inc ah
    int 0x16
    jnz fail
    ret
fail:
    mov [failure_values],ax
    mov [failure_values+2],bx
    mov [failure_values+4],bp
    xor ax,ax
    mov al,[last_make]
    mov [failure_values+6],ax
    mov dx,failed
    mov ah,9
    int 0x21
    mov si,failure_values
    mov cx,4
.dump:
    lodsw
    call hex
    mov dl,' '
    mov ah,2
    int 0x21
    loop .dump
    call restore_int9
    mov ax,0x4c01
    int 0x21
hex:
    push bx
    push cx
    mov bx,ax
    mov cx,4
.digit:
    push cx
    mov cl,4
    rol bx,cl
    pop cx
    mov dl,bl
    and dl,15
    add dl,'0'
    cmp dl,'9'
    jbe .emit
    add dl,7
.emit:
    mov ah,2
    int 0x21
    loop .digit
    pop cx
    pop bx
    ret
int9_hook:
    push ax
    in al,0x60
    test al,0x80
    jnz .chain
    mov [cs:last_make],al
.chain:
    pop ax
    jmp far [cs:old_int9]
old_int9 dw 0,0
last_make db 0
expected_scan db 0
failure_values times 4 dw 0
passed db 'DOS MODIFIERS OK',13,10,'$'
failed db 'DOS MODIFIERS FAILED',13,10,'$'
