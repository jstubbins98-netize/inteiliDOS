; CPU bus/BIOS regression and visible EGA/VGA test under real DOS.
bits 16
cpu 8086
org 0x100
start:
    ; A real guest INT 10h hook chaining with PUSHF/FAR CALL.
    xor ax,ax
    mov es,ax
    mov ax,[es:0x40]
    mov [old10],ax
    mov ax,[es:0x42]
    mov [old10+2],ax
    cli
    mov word [es:0x40],video_hook
    mov ax,cs
    mov [es:0x42],ax
    sti
    mov ax,0x13
    int 0x10
    cli
    mov ax,[old10]
    mov [es:0x40],ax
    mov ax,[old10+2]
    mov [es:0x42],ax
    sti
    mov ah,0xf
    int 0x10
    cmp al,0x13
    jne fail
    mov ax,0xa000
    mov es,ax
    mov ax,0xc09
    xor bx,bx
    mov cx,10
    mov dx,10
    int 0x10
    mov ah,0xd
    int 0x10
    cmp al,9
    jne fail
    ; Aligned DOS file reads may DMA directly to the VGA aperture.
    mov dx,pixel_file
    mov ax,0x3d00
    int 0x21
    jc fail
    mov bx,ax
    push ds
    mov ax,es
    mov ds,ax
    xor dx,dx
    mov cx,2048
    mov ah,0x3f
    int 0x21
    pop ds
    jc fail
    cmp ax,2048
    jne fail
    mov ah,0x3e
    int 0x21
    cmp byte [es:0],0x12
    jne fail
    cmp byte [es:2047],0x12
    jne fail
    xor di,di
    mov ax,0x0404
    mov cx,32000
    rep stosw
    ; Byte/word memory operands, XCHG, shifts, ALU, and string paths.
    mov word [es:321],0x0201
    mov ax,[es:321]
    cmp ax,0x0201
    jne fail
    mov al,3
    xchg al,[es:321]
    cmp al,1
    jne fail
    shl byte [es:321],1
    cmp byte [es:321],6
    jne fail
    inc byte [es:321]
    mov al,[es:321]
    cmp al,7
    jne fail
    mov si,321
    mov di,325
    push ds
    mov ax,es
    mov ds,ax
    movsb
    pop ds
    cmp byte [es:325],7
    jne fail
    call wait_key                ; red VGA frame

    mov ax,0x0d
    int 0x10
    mov dx,0x3c4
    mov ax,0x0f02
    out dx,ax                    ; word OUT to index/data pair
    in ax,dx
    cmp ax,0x0f02                 ; word IN reads index and selected register
    jne fail
    mov dx,0x3ce
    mov ax,0x0205                 ; write mode 2
    out dx,ax
    xor di,di
    mov al,2
    mov cx,8000
    rep stosb                    ; green EGA frame
    mov ax,0x0005
    out dx,ax                    ; read/write mode 0
    mov ax,0x0104                 ; read green plane
    out dx,ax
    cmp byte [es:0],255
    jne fail
    mov ax,0x0004
    out dx,ax
    cmp byte [es:0],0
    jne fail
    ; Latch copy must not read its destination before writing (mode 1).
    mov ax,0x0205
    out dx,ax
    mov byte [es:8000],5
    mov ax,0x0005
    out dx,ax
    mov al,[es:8000]
    mov ax,0x0105
    out dx,ax
    mov si,8000
    xor di,di
    push ds
    mov ax,es
    mov ds,ax
    movsb
    pop ds
    mov ax,0x0005
    out dx,ax
    cmp byte [es:0],255           ; plane 0 copied from source
    jne fail
    cmp byte [es:1],0             ; neighboring green pixel unchanged
    jne fail
    call wait_key

    mov ax,0x12
    int 0x10
    mov dx,0x3ce
    mov ax,0x0205
    out dx,ax
    xor di,di
    mov al,1
    mov cx,38400
    rep stosb                    ; blue 640x480 frame, fitted to host VGA
    call wait_key

    ; BIOS palette and chain-4 -> unchained Mode X page addressing.
    mov ax,0x13
    int 0x10
    mov dx,0x3c4
    mov ax,0x0604
    out dx,ax
    mov ax,0x0f02
    out dx,ax
    xor di,di
    mov al,14
    mov cx,16000
    rep stosb                    ; yellow unchained frame
    call wait_key
    mov dx,0x3c8
    mov al,14
    out dx,al
    inc dx
    mov al,63
    out dx,al
    xor al,al
    out dx,al
    mov al,63
    out dx,al
    call wait_key                ; same pixels, now magenta through the DAC

    mov ax,3
    int 0x10
    mov dx,success
    jmp message
fail:
    mov ax,3
    int 0x10
    mov dx,failure
message:
    mov ah,9
    int 0x21
    mov ax,0x4c00
    int 0x21
wait_key:
    xor ah,ah
    int 0x16
    ret
video_hook:
    pushf
    call far [cs:old10]
    iret
old10 dd 0
pixel_file db 'A:\PIXELS.BIN',0
success db 'DOS EGA VGA BUS AND BIOS OK',13,10,'$'
failure db 'DOS EGA VGA TEST FAILED',13,10,'$'
