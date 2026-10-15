; Real DOS COM fixture: 80186 stack/ALU, CGA output, isolated PIT, and exit.
bits 16
cpu 186
org 0x100
mov bp,0x1234
mov dx,failed_stack
pusha
xor ax,ax
popa
cmp bp,0x1234
jne fail
mov dx,failed_enter
enter 4,0
leave
cmp bp,0x1234
jne fail
mov dx,failed_push
push byte -1
pop bx
cmp bx,0xffff
jne fail
mov dx,failed_imul
imul ax,bx,-3
cmp ax,3
jne fail
mov dx,cpu_ok
mov ah,9
int 0x21
xor ah,ah
int 0x16
mov ax,4
int 0x10
mov ax,0xb800
mov es,ax
xor di,di
mov ax,0xaaaa
mov cx,0x2000
rep stosw
mov al,0xb6
out 0x43,al
mov ax,2712
out 0x42,al
mov al,ah
out 0x42,al
in al,0x61
or al,3
out 0x61,al
xor ah,ah
int 0x16
in al,0x61
and al,0xfc
out 0x61,al
mov ax,3
int 0x10
mov dx,graphics_ok
mov ah,9
int 0x21
mov ax,0x4c2a
int 0x21
fail:
mov ah,9
int 0x21
mov ax,0x4c01
int 0x21
cpu_ok db 'CPU186 OK - press Space for CGA',13,10,'$'
graphics_ok db 'CGA AND SPEAKER OK',13,10,'$'
failed_stack db 'CPU186 PUSHA/POPA FAILED',13,10,'$'
failed_enter db 'CPU186 ENTER/LEAVE FAILED',13,10,'$'
failed_push db 'CPU186 PUSH IMM FAILED',13,10,'$'
failed_imul db 'CPU186 IMUL FAILED',13,10,'$'
