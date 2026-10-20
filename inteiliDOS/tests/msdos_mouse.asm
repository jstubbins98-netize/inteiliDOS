bits 16
cpu 8086
org 0x100
    mov bp,1
    xor ax,ax
    int 0x33
    cmp ax,0xffff
    jne fail
    cmp bx,3
    jne fail
    mov bp,2
    xor ax,ax
    mov es,ax
    cmp word [es:0xce],0xf000
    jne fail
    mov bp,3
    mov ax,3
    pushf
    call far [es:0xcc] ; test driver chaining, not just INT interception
    cmp cx,320
    jne fail
    cmp dx,100
    jne fail
    mov bp,4
    mov ax,15
    mov cx,8
    mov dx,8
    int 0x33
    mov ax,4
    mov cx,160
    mov dx,80
    int 0x33
    push cs
    pop es
    mov ax,12
    mov cx,127
    mov dx,callback
    int 0x33
    mov ax,1
    int 0x33
    mov dx,move_prompt
    call puts
.move:
    mov ax,3
    int 0x33
    cmp cx,160
    jbe .move
    mov dx,left_prompt
    call puts
    mov si,1
    call press_release
    mov dx,right_prompt
    call puts
    mov si,2
    call press_release
    mov dx,middle_prompt
    call puts
    mov si,4
    call press_release
    mov bp,5
    mov ax,5
    mov bx,0
    int 0x33
    cmp bx,1
    jne fail
    mov ax,6
    mov bx,2
    int 0x33
    cmp bx,1
    jne fail
    mov bp,6
.events:
    cmp word [events],127
    jne .events ; callbacks are asynchronous, including the final button release
    ; Event handler register changes must not escape into the interrupted code.
    mov ax,12
    xor cx,cx
    xor dx,dx
    int 0x33
    mov dx,passed
    call puts
    ; Enter graphics and expose the composited pointer without damaging VRAM.
    xor ax,ax
    int 0x16
    mov ax,0x13
    int 0x10
    mov ax,4
    mov cx,320
    mov dx,100
    int 0x33
    mov ax,1
    int 0x33
    mov ax,0xa000
    mov es,ax
    xor di,di
    mov cx,64000
    mov al,1
    rep stosb
.graphics:
    mov ah,1
    int 0x16
    jz .graphics
    ; F12 controls the native overlay and must not leak into the DOS queue.
    mov ax,3
    int 0x10
    jmp fail
press_release:
.press:
    mov ax,3
    int 0x33
    test bx,si
    jz .press
.release:
    mov ax,3
    int 0x33
    test bx,si
    jnz .release
    ret
callback:
    push ds
    push cs
    pop ds
    or [events],ax
    pop ds
    mov ax,0xdead
    mov bx,0xbeef
    mov cx,0xeeee
    mov dx,0xdddd
    mov si,0xcccc
    mov di,0xbbbb
    retf
puts:
    mov ah,9
    int 0x21
    ret
fail:
    mov dx,failed
    call puts
    mov dx,bp
    add dl,'0'
    mov ah,2
    int 0x21
    mov ax,0x4c01
    int 0x21
events: dw 0
move_prompt: db 'MOUSE READY - MOVE RIGHT',13,10,'$'
left_prompt: db 'CLICK LEFT',13,10,'$'
right_prompt: db 'CLICK RIGHT',13,10,'$'
middle_prompt: db 'CLICK MIDDLE',13,10,'$'
passed: db 'DOS MOUSE OK - ENTER FOR VGA',13,10,'$'
failed: db 'DOS MOUSE FAILED',13,10,'$'
