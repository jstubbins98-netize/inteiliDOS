; Private DOS disk only: relocate, load partition at LBA 1 (CHS 0/0/2).
bits 16
org 0x7c00
cli
xor ax, ax
mov ds, ax
mov es, ax
mov ss, ax
mov sp, 0x7c00
sti
cld
mov si, 0x7c00
mov di, 0x600
mov cx, 256
rep movsw
jmp 0:relocated - $$ + 0x600
relocated:
mov ax, 0x0201
mov bx, 0x7c00
mov cx, 2
mov dx, 0x0080
int 0x13
jc failed
jmp 0:0x7c00
failed:
hlt
jmp failed
