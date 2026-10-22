bits 16
cpu 286
org 0x100
%ifdef SB16
%define BYTES 32000
%else
%define BYTES 16000
%endif
push cs
pop ds
push cs
pop es
cld
mov dx,0x226
mov al,1
out dx,al
mov cx,40
delay:in al,dx
loop delay
xor al,al
out dx,al
mov dx,0x22e
mov cx,1000
detect:
in al,dx
test al,0x80
jnz ready
loop detect
jmp absent
ready:
mov dx,0x22a
in al,dx
cmp al,0xaa
jne absent
mov dx,detected
call print
; Choose a buffer that does not cross an ISA DMA 64 KB boundary.
mov si,buffer
mov ax,cs
mov bx,16
mul bx
add ax,si
adc dx,0
cmp ax,65536-BYTES
jbe aligned_buffer
neg ax
add si,ax
mov ax,cs
mul bx
add ax,si
adc dx,0
aligned_buffer:
mov [physical],ax
mov [page],dl
mov di,si
%ifdef SB16
mov cx,2000
fill:
mov ax,0xa000
stosw
stosw
stosw
stosw
mov ax,0x6000
stosw
stosw
stosw
stosw
loop fill
%else
mov cx,2000
fill:
mov ax,0x2020
stosw
stosw
mov ax,0xe0e0
stosw
stosw
loop fill
%endif
; Save PIC mask and install the guest's IRQ5 (INT 0D) handler.
in al,0x21
mov [old_mask],al
and al,0xdf
out 0x21,al
xor ax,ax
mov es,ax
cli
mov ax,[es:0x34]
mov [old_vector],ax
mov ax,[es:0x36]
mov [old_vector+2],ax
mov word [es:0x34],handler
mov ax,cs
mov [es:0x36],ax
sti
%ifdef SB16
mov dx,0xd4
mov al,5
out dx,al
mov dx,0xd8
xor al,al
out dx,al
mov ax,[physical]
shr ax,1
test byte [page],1
jz word_address
or ax,0x8000
word_address:
mov dx,0xc4
out dx,al
mov al,ah
out dx,al
mov al,[page]
out 0x8b,al
mov ax,15999
mov dx,0xc6
out dx,al
mov al,ah
out dx,al
mov dx,0xd6
%ifdef AUTO
mov al,0x59
%else
mov al,0x49
%endif
out dx,al
mov dx,0xd4
mov al,1
out dx,al
mov dx,0x22c
mov al,0x41
out dx,al
mov al,0x1f
out dx,al
mov al,0x40
out dx,al
%ifdef AUTO
mov al,0xb6
%else
mov al,0xb0
%endif
out dx,al
mov al,0x10
out dx,al
%ifdef AUTO
mov ax,7999
%else
mov ax,15999
%endif
out dx,al
mov al,ah
out dx,al
%else
mov al,5
out 0x0a,al
xor al,al
out 0x0c,al
mov ax,[physical]
out 2,al
mov al,ah
out 2,al
mov al,[page]
out 0x83,al
mov ax,15999
out 3,al
mov al,ah
out 3,al
%ifdef AUTO
mov al,0x59
%else
mov al,0x49
%endif
out 0x0b,al
mov al,1
out 0x0a,al
mov dx,0x22c
mov al,0xd1
out dx,al
mov al,0x40
out dx,al
mov al,131
out dx,al
%ifdef AUTO
mov al,0x48
out dx,al
mov ax,7999
out dx,al
mov al,ah
out dx,al
mov al,0x1c
out dx,al
%else
mov al,0x14
out dx,al
mov ax,15999
out dx,al
mov al,ah
out dx,al
%endif
%endif
wait_irq:
%ifdef AUTO
cmp byte [events],3
jb wait_irq
%ifdef SB16
mov al,0xd9
%else
mov al,0xda
%endif
out dx,al
wait_finish:
cmp byte [events],4
jb wait_finish
%else
cmp byte [events],1
jb wait_irq
%endif
mov al,0xd3
out dx,al
cli
mov ax,[old_vector]
mov [es:0x34],ax
mov ax,[old_vector+2]
mov [es:0x36],ax
mov al,[old_mask]
out 0x21,al
sti
mov dx,success
call print
mov ax,0x4c00
int 0x21
absent:
mov dx,no_device
call print
mov ax,0x4c00
int 0x21
handler:
push ax
push dx
%ifdef SB16
mov dx,0x22f
%else
mov dx,0x22e
%endif
in al,dx
mov al,0x20
out 0x20,al
inc byte [cs:events]
pop dx
pop ax
iret
print:
mov ah,9
int 0x21
ret
physical dw 0
page db 0
events db 0
old_mask db 0
old_vector dd 0
detected db 'DSP DETECTED',13,10,'$'
success db 'SOUND IRQ OK',13,10,'$'
no_device db 'NO DSP DETECTED',13,10,'$'
align 2
buffer times BYTES*2 db 0
