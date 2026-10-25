; AdLib's standard timer detection, then a sustained ~440Hz FM note.
; Leave it playing across subsequent PCM programs and DSP resets; F8 stops it.
bits 16
cpu 286
org 0x100
push cs
pop ds
mov ax,0x6004
call reg
mov ax,0x8004
call reg
mov dx,0x388
in al,dx
and al,0xe0
jnz absent
mov ax,0xff02
call reg
mov ax,0x2104
call reg
mov cx,200
poll:
in al,dx
loop poll
and al,0xe0
cmp al,0xc0
jne absent
mov ax,0x6004
call reg
mov ax,0x8004
call reg
mov si,instrument
mov cx,(instrument_end-instrument)/2
program:
lodsw
call reg
loop program
mov dx,success
jmp print
absent:
mov dx,no_fm
print:
mov ah,9
int 0x21
mov ax,0x4c00
int 0x21
; AH=data, AL=address; preserve CX, use normal AdLib bus delays.
reg:
push cx
mov dx,0x388
out dx,al
push ax
mov cx,6
address_delay:
in al,dx
loop address_delay
pop ax
inc dx
mov al,ah
out dx,al
dec dx
mov cx,35
data_delay:
in al,dx
loop data_delay
pop cx
ret
instrument:
dw 0x2001,0x2120,0x2123,0x3f40,0x0043
dw 0xf060,0xf063,0x0f80,0x0f83,0x31c0,0x44a0,0x32b0
instrument_end:
success db 'FM DETECTION OK - MUSIC PLAYING',13,10,'$'
no_fm db 'NO FM DETECTED',13,10,'$'
