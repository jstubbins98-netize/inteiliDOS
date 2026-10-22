; Original fixture: closed-A20 data, REP, instruction-fetch and disk addressing.
; No Microsoft/game executable is embedded in this regression.
bits 16
cpu 286
org 0x100
push cs
pop ds
pushf
pop word [flags]
cli
cld
xor ax,ax
mov es,ax
mov si,0x700
mov di,saved
mov cx,512
.save:
mov al,[es:si]
mov [di],al
inc si
inc di
loop .save
mov ax,[es:0]
mov [vectors],ax
mov ax,[es:2]
mov [vectors+2],ax
mov ax,[es:4]
mov [vectors+4],ax
mov ax,[es:6]
mov [vectors+6],ax
mov ax,0xffff
mov es,ax
mov al,[es:15]
mov [last],al
; FFFF:0710 must alias 0000:0700, not read uninitialized HMA.
mov word [es:0x710],0x5aa5
xor ax,ax
mov es,ax
cmp word [es:0x700],0x5aa5
jne fail
mov byte [stage],'W'
; Word access across physical FFFFFh must split across the wrap.
mov ax,0xffff
mov es,ax
mov word [es:15],0x1234
cmp word [es:15],0x1234
jne fail
xor ax,ax
mov es,ax
cmp byte [es:0],0x12
jne fail
mov byte [stage],'R'
; EXEPACK uses descending strings through underflowed segment registers.
mov word [es:0x710],0x8028
push ds
mov ax,0xffff
mov ds,ax
mov si,0x721
mov di,0x731
mov cx,2
std
rep movsb
cld
pop ds
cmp word [es:0x730],0x8028
jne fail
mov byte [stage],'F'
; MOV AX,0286h straddles FFFFFh, then a far JMP starts above 1 MiB.
; Interrupts stay disabled until all altered vectors have been restored.
mov word [es:0],0x0286
mov byte [es:2],0xea
mov word [es:3],fetched
mov word [es:5],cs
mov ax,0xffff
mov es,ax
mov byte [es:15],0xb8
jmp 0xffff:15
fetched:
cmp ax,0x0286
jne fail
; A nonzero CS:IP aliasing physical zero is code, not the 0000:0000 exit.
xor ax,ax
mov es,ax
mov word [es:0],0x86b8
mov byte [es:2],0x02
mov byte [es:3],0xea
mov word [es:4],fetched_zero
mov word [es:6],cs
jmp 0xffff:0x10
fetched_zero:
cmp ax,0x0286
jne fail
mov byte [stage],'D'
; BIOS disk transfers must use the same address bus as CPU operands.
; Restore the IVT and low DOS-resident memory before invoking BIOS services.
call restore_memory
mov ax,cs
cmp ax,0x1000
jae fail
mov cl,4
shl ax,cl
add ax,sector_data
jc fail
cmp ax,0xfe00
ja fail
add ax,16
mov bx,ax
mov ax,0xffff
mov es,ax
mov ax,0x0201
xor dx,dx
mov cx,1
int 0x13
jc fail
cmp word [sector_data+510],0xaa55
jne fail
mov byte [failed],0
fail:
call restore_memory
push word [flags]
popf
cmp byte [failed],0
je .ok
mov dl,[stage]
mov ah,2
int 0x21
mov dx,failure
jmp .print
.ok:
mov dx,success
.print:
mov ah,9
int 0x21
mov ax,0x4c00
int 0x21
restore_memory:
cld
xor ax,ax
mov es,ax
mov si,saved
mov di,0x700
mov cx,512
rep movsb
mov ax,[vectors]
mov [es:0],ax
mov ax,[vectors+2]
mov [es:2],ax
mov ax,[vectors+4]
mov [es:4],ax
mov ax,[vectors+6]
mov [es:6],ax
mov ax,0xffff
mov es,ax
mov al,[last]
mov [es:15],al
ret
flags dw 0
vectors times 8 db 0
last db 0
stage db 'B'
failed db 1
saved times 512 db 0
sector_data times 512 db 0
success db 'A20 WRAP TEST OK',13,10,'$'
failure db ' A20 WRAP TEST FAILED',13,10,'$'
