; Freestanding 486SX/DX CPU fixture: real -> protected -> paged -> real mode.
bits 16
cpu 486
org 0x100
%ifdef DOS
%define RESULT 0x2f0000
%define PF_RESULT 0x2f0004
%define IDT 0x2f1000
%else
%define RESULT 0x700
%define PF_RESULT 0x704
%define IDT 0x8000
%endif
start:
%ifdef DOS
    mov ax,0x2401
    int 0x15
    push cs
    pop ds
    mov ax,cs
    mov [real_jump+3],ax
    movzx eax,ax
    shl eax,4
    mov [gdt+8+2],ax
    mov [gdt+24+2],ax
    shr eax,16
    mov [gdt+8+4],al
    mov [gdt+24+4],al
    movzx eax,word [real_jump+3]
    shl eax,4
    add eax,gdt
    mov [gdtr+2],eax
%endif
    cli
%ifndef DOS
    xor ax,ax
    mov ds,ax
    mov es,ax
    mov ss,ax
    mov sp,0x9000
%endif
    lgdt [gdtr]
    mov eax,cr0
    or al,1
    mov cr0,eax
    jmp dword 8:protected
align 8
gdt:
    dq 0
    dq 0x00cf9a000000ffff
    dq 0x00cf92000000ffff
    dq 0x00009a000000ffff
gdtr: dw $-gdt-1
    dd gdt
idtr: dw 256*8-1
    dd IDT
real_idtr: dw 0x3ff
    dd 0
bits 32
protected:
    mov ax,0x10
    mov ds,ax
    mov es,ax
    mov ss,ax
    mov fs,ax
    mov gs,ax
    mov esp,0x90000
    mov eax,1
    db 0x0f,0xa2 ; optional late-486 CPUID encoding
    cmp eax,0x400
    jne fail
    test edx,0x110 ; x87 optional; no TSC or CMPXCHG8B on either 486 model
    jnz fail
    mov eax,0x12345678
    bswap eax
    cmp eax,0x78563412
    jne fail
    mov ebx,5
    mov ecx,7
    xadd ebx,ecx
    cmp ebx,12
    jne fail
    cmp ecx,5
    jne fail
    mov eax,12
    mov edx,0xabcdef01
    db 0x0f,0xb1,0xd3 ; CMPXCHG EBX,EDX (486 encoding)
    jne fail
    cmp ebx,edx
    jne fail
    mov eax,1
    shld eax,eax,4
    cmp eax,16
    jne fail
    mov eax,0x76543210
    mov [0x300000],eax
    ; Identity-map 0..4MiB; alias virtual 4MiB to physical 3MiB.
    mov edi,0x100000
    xor eax,eax
    mov ecx,4096
    rep stosd
    mov dword [0x100000],0x101003
    mov dword [0x100004],0x102003
    mov edi,0x101000
    mov eax,3
    mov ecx,1024
.pte:
    stosd
    add eax,4096
    loop .pte
    mov dword [0x102000],0x300003
    mov dword [0x500000],0xfeedbeef
    ; Protected-mode #PF gate. Handler maps the missing page and retries.
    mov eax,page_fault
    mov word [IDT+14*8],ax
    mov word [IDT+14*8+2],8
    mov word [IDT+14*8+4],0x8e00
    shr eax,16
    mov word [IDT+14*8+6],ax
    lidt [cs:idtr]
    mov eax,0x100000
    mov cr3,eax
    mov eax,cr0
    or eax,0x80000000
    mov cr0,eax
    mov eax,[0x400000]
    cmp eax,0x76543210
    jne fail
    mov dword [0x400000],0x11223344
    cmp dword [0x300000],0x11223344
    jne fail
    mov eax,[0x800000]
    cmp eax,0xfeedbeef
    jne fail
    cmp dword [PF_RESULT],0x5046
    jne fail
    test byte [0x102000],0x40
    jz fail
    mov eax,cr0
    and eax,0x7fffffff
    mov cr0,eax
    jmp word 0x18:pm16
page_fault:
    push eax
    mov eax,cr2
    cmp eax,0x800000
    jne fail
    mov dword [0x100008],0x103003
    mov dword [0x103000],0x500003
    invlpg [0x800000]
    mov dword [PF_RESULT],0x5046
    pop eax
    add esp,4
    iretd
fail:
    mov dword [RESULT],0xdead
    hlt
    jmp $
bits 16
pm16:
    mov eax,cr0
    and al,0xfe
    mov cr0,eax
real_jump:
    jmp 0:real
real:
%ifdef DOS
    push cs
    pop ds
    mov ax,ds
    mov es,ax
    mov ss,ax
    mov sp,0xff00
    lidt [real_idtr]
    sti
    mov dx,success
    mov ah,9
    int 0x21
    mov ax,0x2400
    int 0x15
    mov ax,0x4c00
    int 0x21
success: db '486 PROTECTED PAGING PASS',13,10,'$'
%else
    xor ax,ax
    mov ds,ax
    mov es,ax
    mov ss,ax
    mov dword [RESULT],0x486600d
    hlt
    jmp $
%endif
