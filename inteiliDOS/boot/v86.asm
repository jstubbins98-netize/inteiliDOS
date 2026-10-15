; 80386 v86 entry/return. All guest hardware access is trapped (IOPL=0).
BITS 32
SECTION .text
GLOBAL v86_enter, v86_return
EXTERN dos_cleanup
v86_enter:
    pushfd
    cli
    pushad
    mov [saved_esp], esp
    mov eax, [esp + 40] ; pointer to {ip,cs,sp,ss,ds,es}, dwords
    push dword 0       ; GS
    push dword 0       ; FS
    push dword [eax+20]; ES
    push dword [eax+16]; DS
    push dword [eax+12]; SS
    push dword [eax+8] ; SP
    push dword 0x20202 ; VM=1, IF=1, IOPL=0, reserved bit 1
    push dword [eax+4] ; CS
    push dword [eax]   ; IP
    xor eax, eax
    xor ebx, ebx
    xor ecx, ecx
    xor edx, edx
    xor esi, esi
    xor edi, edi
    xor ebp, ebp
    iretd
v86_return:
    cli
    cld
    mov ax, 0x10
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    mov esp, [saved_esp]
    call dos_cleanup
    popad
    popfd
    ret
SECTION .bss
ALIGN 4
saved_esp: resd 1
SECTION .note.GNU-stack noalloc noexec nowrite progbits
