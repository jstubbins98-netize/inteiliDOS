; Synthetic DOS programs, built by run_dos_tests.py; no copyrighted binaries.
BITS 16
%ifndef CASE
%define CASE 0
%endif
%if CASE = 1
; MZ header with one real segment relocation, CS:IP = image:0.
ORG 0
dw 0x5A4D
dw (file_end-$$) % 512
dw ((file_end-$$)+511)/512
dw 1, 2, 0x100, 0xFFFF
dw 0, 0, 0, 0, 0, 28, 0  ; SP=0 represents the top of a 64 KB stack
dw relocated-image_start, 0
image_start:
    push ax
    pop ax
    mov ax, [cs:relocated-image_start]
    mov dx, cs
    cmp ax, dx
    jne fail_mz
    mov ax, 0x4C2A
    int 0x21
fail_mz:
    mov ax, 0x4CEE
    int 0x21
relocated: dw 0
file_end:
%else
ORG 0x100
%if CASE = 2
    in al, 0x21                 ; must not touch the real PIC
%elif CASE = 3
    mov ax, 0xFFFF
    mov ds, ax
    mov ax, [0x100]              ; beyond the guest address space
%elif CASE = 4
    mov ax, 0x4B00              ; unsupported EXEC must return an error screen
    int 0x21
%elif CASE = 5
    mov ax, 0x0013              ; graphics BIOS is deliberately unsupported
    int 0x10
%elif CASE = 6
    ret                         ; COM stack zero -> PSP INT 20h
%elif CASE = 7
    mov ah, 8
    int 0x21                   ; blocking input remains interruptible with F8
    mov ax, 0x4CEE
    int 0x21
%elif CASE = 8
    cli
.busy:
    jmp .busy                  ; guest CLI must not disable host timer/abort
%elif CASE = 9
    ; Far-pointer protected-memory access, not merely a bad segment wrap.
    a32 mov byte [0x100000], 0
%elif CASE = 10
    xor ax, ax
    mov ds, ax
    mov word [0x413], 123       ; write private BDA, never the real BIOS page
    mov ax, 0x4C00
    int 0x21
%else
    cld
    mov bp, 0xE0
    cli
    pushf
    pop ax
    test ax, 0x200
    jnz fail
    sti
    pushf
    pop ax
    test ax, 0x200
    jz fail
    pushfd
    pop eax
    test eax, 0x23000           ; VM/IOPL hidden in virtual PUSHFD
    jnz fail
    mov dx, hello
    mov ah, 9
    std                        ; monitor must clear DF while executing C
    int 0x21
    cld
    mov ah, 0x30
    int 0x21
    cmp ax, 5
    jne fail
    inc bp
    ; Hook INT 21h and chain through its saved vector. Failed file open must
    ; still return carry through the monitor stub's emulated IRET.
    mov ax, 0x3521
    int 0x21
    mov [old21], bx
    mov [old21+2], es
    mov dx, chain21
    mov ax, 0x2521
    int 0x21
    mov dx, missing
    mov ax, 0x3D00
    int 0x21
    jnc fail
    cmp ax, 2
    jne fail
    inc bp
    push ds
    mov dx, [old21]
    mov ds, [old21+2]
    mov ax, 0x2521
    int 0x21
    pop ds
    inc bp
    push cs
    pop es
    mov bx, 0x1000
    mov ah, 0x4A
    int 0x21
    jc fail
    mov bx, 0x20
    mov ah, 0x48
    int 0x21
    jc fail
    mov es, ax
    mov ah, 0x49
    int 0x21
    jc fail
    inc bp
    mov dx, hook
    mov ax, 0x2560
    int 0x21
    int 0x60
    cmp ax, 0x1234
    jne fail
    mov dx, hook
    mov ax, 0x2500              ; vector zero is valid, not a NULL buffer
    int 0x21
    int 0
    cmp ax, 0x1234
    jne fail
    inc bp
    mov dx, filename
    mov ax, 0x3D00
    int 0x21
    jc fail
    mov bx, ax
    mov cx, 4
    mov dx, buffer
    mov ah, 0x3F
    int 0x21
    jc fail
    cmp ax, 4
    jne fail
    cmp word [buffer], 'WX'
    jne fail
    cmp word [buffer+2], 'YZ'
    jne fail
    mov ax, 0x4200
    xor cx, cx
    mov dx, 2
    int 0x21
    jc fail
    cmp ax, 2
    jne fail
    mov cx, 8
    mov dx, buffer
    mov ah, 0x3F
    int 0x21
    jc fail
    cmp ax, 2                   ; EOF truncation
    jne fail
    cmp word [buffer], 'YZ'
    jne fail
    mov ah, 0x3F
    int 0x21
    jc fail
    test ax, ax                 ; EOF returns zero
    jnz fail
    mov cx, 1
    mov ah, 0x40
    int 0x21
    jnc fail                    ; writes must be denied
    cmp ax, 5
    jne fail
    mov ah, 0x3E
    int 0x21
    jc fail
    mov ax, 0x0E21
    int 0x10
    mov ax, 0x4C2A
    int 0x21
fail:
    mov ax, bp
    mov ah, 0x4C
    int 0x21
hook:
    mov ax, 0x1234
    iret
chain21:
    pushf
    call far [cs:old21]
    ; Simulate a DOS TSR preserving the original service's carry flag.
    push bp
    mov bp, sp
    pushf
    pop word [ss:bp+6]
    pop bp
    iret
old21: dw 0,0
hello: db 'DOS v86 test',13,10,'$'
filename: db 'DATA.TXT',0
missing: db 'ABSENT.TXT',0
buffer: times 16 db 0
%endif
%endif
