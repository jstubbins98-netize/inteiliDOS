bits 16
cpu 286
org 0x100
push cs
pop ds
push cs
pop es
cld
; Classic 8086 / 286 / 386 FLAGS probe and PUSH SP.
pushf
pop dx
mov ax,0xf000
push ax
popf
pushf
pop ax
test ax,0xf000
jnz fail
push dx
popf
mov ax,sp
push sp
pop bx
cmp ax,bx
jne fail
mov byte [stage],'1'
; Five-bit counts, displacement-bearing immediates, and rotates through CF.
mov ax,0x8123
mov cl,32
shl ax,cl
cmp ax,0x8123
jne fail
mov cl,33
shl ax,cl
cmp ax,0x0246
jne fail
mov bx,shift_value-2
shl word [bx+2],0xff
cmp word [shift_value],0
jne fail
mov al,0x80
mov cl,9
stc
rcl al,cl
jnc fail
cmp al,0x80
jne fail
mov byte [stage],'2'
; 0F is no longer a host escape outside the original firmware.
smsw ax
test ax,1
jnz fail
mov ax,14
lmsw ax
smsw bx
and bx,15
cmp bx,14
jne fail
clts
smsw bx
and bx,15
cmp bx,6
jne fail
xor ax,ax
lmsw ax
lgdt [gdt]
sgdt [gdt_read]
mov si,gdt
mov di,gdt_read
mov cx,5
repe cmpsb
jne fail
cmp byte [gdt_read+5],255
jne fail
mov byte [stage],'3'
; An independently relocated real-mode IDT; host IRQs remain unchanged.
sidt [idt_saved]
push ds
xor ax,ax
mov ds,ax
xor si,si
mov di,idt_copy
mov cx,512
rep movsw
pop ds
mov word [idt_copy+0x60*4],test_interrupt
mov ax,cs
mov [idt_copy+0x60*4+2],ax
mov bx,16
mul bx
add ax,idt_copy
adc dx,0
mov [idt_new+2],ax
mov [idt_new+4],dl
cli
lidt [idt_new]
int 0x60
lidt [idt_saved]
sti
cmp byte [interrupted],1
jne fail
mov byte [stage],'4'
; Install restartable #BR, #UD and divide-fault handlers.
xor ax,ax
mov es,ax
cli
mov si,0
mov di,saved_vectors
mov cx,14
save_vectors:
mov ax,[es:si]
mov [di],ax
add si,2
add di,2
loop save_vectors
mov word [es:0],fault_handler
mov word [es:5*4],fault_handler
mov word [es:6*4],fault_handler
mov ax,cs
mov [es:2],ax
mov [es:5*4+2],ax
mov [es:6*4+2],ax
sti
mov ax,-10
bound ax,[bounds]
mov ax,10
bound ax,[bounds]
mov word [expected_fault],bound_fault
mov word [resume],after_bound
mov ax,11
bound_fault:
cs bound ax,[bounds]          ; exception IP must include the prefix
after_bound:
cmp byte [faults],1
jne cleanup_fail
mov word [expected_fault],invalid_fault
mov word [resume],after_invalid
invalid_fault:
db 0x0f,0x00,0xc0            ; SLDT is #UD in real mode, not PUTCHAR_AL
after_invalid:
cmp byte [faults],2
jne cleanup_fail
mov word [expected_fault],divide_fault
mov word [resume],after_divide
mov ax,1
xor bx,bx
divide_fault:
div bx
after_divide:
cmp byte [faults],3
jne cleanup_fail
call restore_vectors
mov byte [stage],'5'
; String port I/O shares the scalar register model (and never native ports).
push cs
pop es
mov dx,0x300
mov si,port_bytes
mov cx,3
rep outsb
mov di,port_read
mov cx,3
rep insb
cmp word [port_read],0x3333
jne fail
cmp byte [port_read+2],0x33
jne fail
std
mov si,port_words+2
mov cx,2
rep outsw
mov di,port_read+2
mov cx,2
rep insw
cld
cmp word [port_read],0x1234
jne fail
cmp word [port_read+2],0x1234
jne fail
mov byte [stage],'6'
; Zero-count REP does no I/O and leaves both index registers untouched.
mov si,port_words
xor cx,cx
rep outsw
cmp si,port_words
jne fail
mov dx,success
jmp print
cleanup_fail:
call restore_vectors
fail:
cld
mov dl,[stage]
mov ah,2
int 0x21
mov dx,failure
print:
mov ah,9
int 0x21
mov ax,0x4c00
int 0x21
restore_vectors:
cli
push es
xor ax,ax
mov es,ax
mov si,saved_vectors
xor di,di
mov cx,14
rep movsw
pop es
sti
ret
fault_handler:
push bp
mov bp,sp
push ax
mov ax,[cs:expected_fault]
cmp [ss:bp+2],ax
jne .bad
inc byte [cs:faults]
.bad:
mov ax,[cs:resume]
mov [ss:bp+2],ax
pop ax
pop bp
iret
test_interrupt:
inc byte [cs:interrupted]
iret
shift_value dw 0x8123
stage db '0'
bounds dw -10,10
gdt dw 0x1234
db 0x56,0x78,0x09,0x55
gdt_read times 6 db 0
idt_saved times 6 db 0
idt_new dw 1023
dd 0
interrupted db 0
expected_fault dw 0
resume dw 0
faults db 0
saved_vectors times 28 db 0
port_bytes db 0x11,0x22,0x33
port_words dw 0x1234,0x5678
port_read times 4 db 0
success db 'REAL286 CPU TEST OK',13,10,'$'
failure db 'REAL286 CPU TEST FAILED',13,10,'$'
idt_copy times 1024 db 0
