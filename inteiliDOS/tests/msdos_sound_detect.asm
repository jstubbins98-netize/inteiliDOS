; Real DOS game-style Sound Blaster detection. No native port passthrough.
bits 16
cpu 286
org 0x100
    push cs
    pop ds
    cld
    ; Read BLASTER from this DOS process's inherited environment segment.
    mov es,[0x2c]
    xor di,di
.environment:
    cmp byte [es:di],0
    je fail
    push di
    mov si,blaster
    mov cx,blaster_end-blaster
.compare:
    lodsb
    scasb
    jne .next
    loop .compare
    pop di
    jmp .scan
.next:
    pop di
.skip:
    cmp byte [es:di],0
    lea di,[di+1]
    jne .skip
    jmp .environment
.scan:
    ; Older games actively probe each standard base rather than trust BLASTER.
    mov bx,0x210
    xor bp,bp
.base:
    lea dx,[bx+6]
    mov al,1
    out dx,al
    mov cx,40
.delay:
    in al,dx
    loop .delay
    xor al,al
    out dx,al
    lea dx,[bx+14]
    mov cx,100
.ready:
    in al,dx
    test al,0x80
    jnz .read
    loop .ready
    jmp .next_base
.read:
    lea dx,[bx+10]
    in al,dx
    cmp al,0xaa
    jne .next_base
    cmp bx,0x220
    jne fail
    inc bp
.next_base:
    add bx,0x10
    cmp bx,0x260
    jbe .base
    cmp bp,1
    jne fail
    mov dx,0x22c
    mov al,0xe1
    out dx,al
    mov dx,0x22e
    in al,dx
    test al,0x80
    jz fail
    mov dx,0x22a
    in al,dx
%ifdef SB16
    cmp al,4
%else
    cmp al,2
%endif
    jne fail
    in al,dx
%ifdef SB16
    cmp al,5
%else
    cmp al,1
%endif
    jne fail
    ; E0h complementary-byte handshake, used by hardware setup tools.
    mov dx,0x22c
    mov al,0xe0
    out dx,al
    mov al,0x3c
    out dx,al
    mov dx,0x22a
    in al,dx
    cmp al,0xc3
    jne fail
    ; E2h DMA identification must write a single byte to guest channel 1.
    mov ax,cs
    mov bx,16
    mul bx
    add ax,dma_byte
    adc dx,0
    mov bx,ax
    mov si,dx
    mov al,5
    out 0x0a,al
    xor al,al
    out 0x0c,al
    mov ax,bx
    out 2,al
    mov al,ah
    out 2,al
    mov ax,si
    out 0x83,al
    xor al,al
    out 3,al
    out 3,al
    mov al,0x45 ; single transfer, device -> memory, channel 1
    out 0x0b,al
    mov al,1
    out 0x0a,al
    mov dx,0x22c
    mov al,0xe2
    out dx,al
    xor al,al
    out dx,al
    cmp byte [dma_byte],0x40
    jne fail
    in al,8
    test al,2
    jz fail
    ; A reset cancels old probes and must deliver a fresh AA handshake.
    mov dx,0x226
    mov al,1
    out dx,al
    xor al,al
    out dx,al
    mov dx,0x22e
    in al,dx
    test al,0x80
    jz fail
    mov dx,0x22a
    in al,dx
    cmp al,0xaa
    jne fail
    mov dx,success
    jmp print_exit
fail:
    mov dx,failure
print_exit:
    mov ah,9
    int 0x21
    mov ax,0x4c00
    int 0x21
dma_byte: db 0xcc
%ifdef SB16
blaster: db 'BLASTER=A220 I5 D1 H5 T6',0
%else
blaster: db 'BLASTER=A220 I5 D1 T3',0
%endif
blaster_end:
success: db 'SB DETECTION OK - ENV PORTS VERSION DMA',13,10,'$'
failure: db 'SB DETECTION FAILED',13,10,'$'
