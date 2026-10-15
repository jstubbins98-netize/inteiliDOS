; Installed partition-chain MBR. Relocate BEFORE loading VBR over 0x7C00.
bits 16
org 0x600
    cli
    xor ax,ax
    mov ds,ax
    mov es,ax
    mov ss,ax
    mov sp,0x7C00
    cld
    mov si,0x7C00
    mov di,0x600
    mov cx,256
    rep movsw
    jmp 0:relocated
relocated:
    mov [drive],dl
    sti
    mov si,0x600+446
    mov cx,4
.find:
    cmp byte [si],0x80
    je .found
    add si,16
    loop .find
    mov si,no_partition
    jmp error
.found:
    mov eax,[si+8]
    mov [dap+8],eax
    mov byte [attempts],5
.read:
    mov word [dap+2],1
    mov dl,[drive]
    mov si,dap
    mov ah,0x42
    push ds
    push es
    int 0x13
    pop es
    pop ds
    jnc .loaded
    dec byte [attempts]
    jz .failed
    mov dl,[drive]
    xor ah,ah
    push ds
    push es
    int 0x13
    pop es
    pop ds
    jmp .read
.failed:
    mov si,disk_error
    jmp error
.loaded:
    cmp word [0x7DFE],0xAA55
    jne .failed
    mov dl,[drive]
    jmp 0:0x7C00
error:
    cld
    lodsb
    test al,al
    jz .halt
    mov ah,0x0E
    mov bx,7
    push si
    push ds
    int 0x10
    pop ds
    pop si
    jmp error
.halt:
    cli
    hlt
    jmp .halt
drive db 0
attempts db 0
align 4,db 0
dap db 16,0
    dw 1,0x7C00,0
    dq 0
no_partition db 'No bootable partition',13,10,0
disk_error db 'inteiliDOS: HDD boot read failed',13,10,0
times 446-($-$$) db 0
times 64 db 0
dw 0xAA55
