; Loaded once: verify two USB disk changes without restarting this DOS program.
bits 16
cpu 8086
org 0x100
mov byte [expected],'1'
call check_disk
; DOS can reach the original BIOS through PUSHF + FAR CALL, bypassing
; software-INT interception. Verify change support on that path too.
mov ah,0x15
xor dx,dx
pushf
call far [rom_disk]
jc fail
cmp ah,2
jne fail
mov ah,0x16
xor dx,dx
pushf
call far [rom_disk]
jc fail
test ah,ah
jnz fail
; A drive with change-line support must report success when unchanged.
mov ah,0x16
xor dx,dx
int 0x13
jc fail
test ah,ah
jnz fail
mov dx,next_prompt
call prompt
mov byte [expected],'2'
call check_disk
mov dx,back_prompt
call prompt
mov byte [expected],'1'
call check_disk
mov dx,passed
mov ah,9
int 0x21
mov ax,0x4c00
int 0x21
prompt:
; Discard command-line typeahead; only a fresh Space continues this fixture.
mov ax,0x0c00
int 0x21
mov ah,9
int 0x21
wait_space:
xor ah,ah
int 0x16
cmp al,32
jne wait_space
ret
check_disk:
mov dx,filename
mov ax,0x3d00
int 0x21
jc fail
mov bx,ax
mov dx,buffer
mov cx,1
mov ah,0x3f
int 0x21
jc fail
cmp ax,1
jne fail
mov ah,0x3e
int 0x21
mov al,[buffer]
cmp al,[expected]
jne bad_content
ret
bad_content:
mov dx,bad_byte
mov ah,9
int 0x21
mov bl,[buffer]
mov dl,bl
mov cl,4
shr dl,cl
call hex_digit
mov dl,bl
and dl,15
call hex_digit
jmp fail
hex_digit:
add dl,'0'
cmp dl,'9'
jbe print_digit
add dl,7
print_digit:
mov ah,2
int 0x21
ret
fail:
mov dx,failed
mov ah,9
int 0x21
mov ax,0x4c01
int 0x21
filename db 'A:\DISKID.TXT',0
next_prompt db 'USB SWAP: F6 NEXT, THEN SPACE',13,10,'$'
back_prompt db 'USB SWAP: F6 BACK, THEN SPACE',13,10,'$'
passed db 'USB LIVE DISK SWAP OK',13,10,'$'
failed db 'USB LIVE DISK SWAP FAILED',13,10,'$'
bad_byte db 'Unexpected disk byte: 0x$'
expected db 0
buffer db 0
rom_disk dw 0x0caf,0xf000
