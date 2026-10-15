; Installed VBR. Preserve the installer patch ABI, not handwritten offsets.
bits 16
org 0x7C00
    jmp near start
times 0x21-($-$$) db 0
kernel_lba dd 0                    ; installer patches 0x21
drive db 0
times 0x36-($-$$) db 0
iterations db 128                 ; installer patches 0x36
attempts db 0
times 0x40-($-$$) db 0
dap db 16,0                       ; DAP is DWORD aligned
    dw 8,0x8000,0
    dq 0
align 8,db 0
gdt:
    dq 0
    dq 0x00CF9A000000FFFF
    dq 0x00CF92000000FFFF
gdtr:
    dw 23
    dd gdt
times 0xDA-($-$$) db 0
copy_dwords dd 131072              ; installer patches 0xDA
start:
    cli
    xor ax,ax
    mov ds,ax
    mov es,ax
    mov ss,ax
    mov sp,0x7C00
    cld
    mov [drive],dl                 ; only after DS is initialized
    sti
    ; Firmware may leave a splash screen, nonzero text page or palette.
    ; Establish mode 3/page 0 (including BIOS font/palette initialization)
    ; BEFORE disk reads: a video BIOS call after loading can reuse low RAM.
    ; Preserve the saved drive and segment/register state across firmware.
    pushad
    push ds
    push es
    mov ax,0x0003
    int 0x10
    pop es
    pop ds
    popad
    cld
    in al,0x92
    or al,2
    and al,0xFE
    out 0x92,al
    mov eax,[kernel_lba]
    mov [dap+8],eax
.load:
    mov byte [attempts],5
.retry:
    mov word [dap+2],8             ; BIOS may modify transferred count
    mov dl,[drive]                ; never hardcode BIOS drive 0x80
    mov si,dap
    mov ah,0x42
    pushad
    push ds
    push es
    int 0x13
    pop es
    pop ds
    popad
    jnc .read_ok
    dec byte [attempts]
    jnz .retry                    ; bounded cold-start readiness retry
    mov ax,0xB800
    mov es,ax
    mov word [es:0],0x4F45         ; E: boot read failed, no disk writes
.halt:
    cli
    hlt
    jmp .halt
.read_ok:
    add word [dap+6],0x100
    add dword [dap+8],8
    dec byte [iterations]
    jnz .load
    ; No INT 10h firmware reentry after reads; text mode is already ready.
    cli
    lgdt [gdtr]
    mov eax,cr0
    or al,1
    mov cr0,eax
    jmp 0x08:protected
bits 32
protected:
    mov ax,0x10
    mov ds,ax
    mov es,ax
    mov ss,ax
    mov esp,0x90000
    ; Also make older installed kernels (without their own BSS clear) safe.
    ; Native kernel RAM is 1..5 MiB; LaunchPad staging begins at 5 MiB.
    ; Supported full builds need >=8 MiB. Source image/stack are below 1 MiB.
    xor eax,eax
    mov edi,0x100000
    mov ecx,0x100000
    cld
    rep stosd
    mov esi,0x8000
    mov edi,0x100000
    mov ecx,[copy_dwords]
    cld
    rep movsd
    xor eax,eax
    xor ebx,ebx
    jmp 0x08:0x100000
times 510-($-$$) db 0
dw 0xAA55
