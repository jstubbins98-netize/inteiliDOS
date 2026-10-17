; Exercise the actual emulated BIOS INT 16h interface used by DOS installers.
bits 16
cpu 8086
org 0x100
mov si,0x0001
mov dx,prompt_f10
mov bx,0x4400
call check_key
mov dx,prompt_f9
mov bx,0x4300
call check_key
mov dx,prompt_f1
mov bx,0x3b00
call check_key
mov dx,prompt_f5
mov bx,0x3f00
call check_key
; Both BIOS APIs must expose the extended BIOS codes, not hardware 57h/58h.
mov dx,prompt_f11
mov bx,0x8500
call check_key
mov dx,prompt_f12
mov bx,0x8600
call check_key
mov si,0x1011
mov dx,prompt_enh_f11
mov bx,0x8500
call check_key
mov dx,prompt_enh_f12
mov bx,0x8600
call check_key
mov dx,passed
mov ah,9
int 0x21
mov ax,0x4c00
int 0x21
check_key:
; AH=01h/11h must report empty and then peek without consuming the key.
; SI's high/low bytes select read/check services (00h/01h or 10h/11h).
mov ax,si
mov ah,al
int 0x16
jnz fail
mov ah,9
int 0x21
.wait:
mov ax,si
mov ah,al
int 0x16
jz .wait
cmp ax,bx
jne fail
; A second peek must return the same key.
mov ax,si
mov ah,al
int 0x16
jz fail
cmp ax,bx
jne fail
mov ax,si
int 0x16
cmp ax,bx
jne fail
mov ax,si
mov ah,al
int 0x16
jnz fail
ret
fail:
mov dx,failed
mov ah,9
int 0x21
mov ax,0x4c01
int 0x21
prompt_f10 db 'INSTALLER TEST: PRESS F10',13,10,'$'
prompt_f9 db 'PRESS F9',13,10,'$'
prompt_f1 db 'PRESS F1',13,10,'$'
prompt_f5 db 'PRESS F5',13,10,'$'
prompt_f11 db 'LEGACY: PRESS F11',13,10,'$'
prompt_f12 db 'LEGACY: PRESS F12',13,10,'$'
prompt_enh_f11 db 'ENHANCED: PRESS F11',13,10,'$'
prompt_enh_f12 db 'ENHANCED: PRESS F12',13,10,'$'
passed db 'DOS FUNCTION KEYS OK',13,10,'$'
failed db 'DOS FUNCTION KEY FAILED',13,10,'$'
