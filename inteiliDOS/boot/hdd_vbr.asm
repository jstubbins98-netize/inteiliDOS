; Streaming HDD loader. One 4 KiB BIOS bounce buffer, never the whole kernel
; in conventional RAM (which would overwrite the stack, EBDA and VGA ROM).
bits 16
org 0x7C00
    jmp near start
; Build a bounded Multiboot memory map below the boot stack/bounce buffer.
; Only a complete E820 enumeration is published. No guessed CONFIG_RAM_MB.
memory_map:
    xor ax,ax
    mov di,0x5000
    mov cx,1024
    cld
    rep stosw
    mov word [0x5030],0x5100      ; Multiboot mmap_addr (upper half is zero)
    xor ebx,ebx
    mov di,0x5104                ; leave room for Multiboot entry size
    mov bp,64                   ; bound calls even if all entries are disabled
    jmp memory_next
times 0x21-($-$$) db 0
kernel_lba dd 0                   ; installer patches 0x21
    dd 0x324C4449                 ; IDL2 layout/loader marker at 0x25
memory_commit:
    lea ax,[di-0x5104]
    mov [0x502C],ax              ; mmap_length (upper half is zero)
    mov byte [0x5000],0x40       ; MULTIBOOT_FLAG_MMAP only
    ret
times 0x36-($-$$) db 0
iterations dw 128                 ; installer patches 16-bit chunk count
drive db 0
attempts db 0
destination dd 0x100000
align 4,db 0
dap:
    db 0x10,0
    dw 8,0x8000,0
    dq 0
align 8,db 0
gdt:
    dq 0
    dq 0x00CF9A000000FFFF         ; 32-bit code
    dq 0x00CF92000000FFFF         ; flat data
    dq 0x00009A000000FFFF         ; 16-bit transition code
gdtr:
    dw 31
    dd gdt
memory_next:
    mov eax,0xE820
    mov edx,0x534D4150
    mov ecx,24
    mov dword [di+20],1          ; default ACPI enabled for 20-byte replies
    push ds
    push es
    push di
    push bp
    int 0x15
    pop bp
    pop di
    pop es
    pop ds
    jc memory_fail
    cmp eax,0x534D4150
    jne memory_fail
    cmp ecx,20
    jb memory_fail
    cmp ecx,24
    jb .entry
    test byte [di+20],1
    jz .continue
.entry:
    mov dword [di-4],20
    add di,24
.continue:
    test ebx,ebx
    jz memory_commit
    dec bp
    jnz memory_next
memory_fail:
    ret                         ; flags stay zero: no invented usable RAM
times 0xDA-($-$$) db 0
copy_dwords dd 131072             ; installer metadata, iterations * 1024
start:
    cli
    xor ax,ax
    mov ds,ax
    mov es,ax
    mov ss,ax
    mov sp,0x7C00
    cld
    mov [drive],dl
    sti
    ; Establish console/font before disk reads; no later video BIOS reentry.
    pushad
    push ds
    push es
    mov ax,3
    int 0x10
    pop es
    pop ds
    popad
    call memory_map
    mov eax,[kernel_lba]
    mov [dap+8],eax
    ; Reject corrupt load bounds before touching high memory.
    cmp word [iterations],1
    jb failure
    cmp word [iterations],1024
    ja failure
load:
    mov byte [attempts],5
retry:
    mov word [dap+2],8
    mov dl,[drive]
    mov si,dap
    mov ah,0x42
    pushad
    push ds
    push es
    int 0x13
    pop es
    pop ds
    popad
    jnc read_ok
    dec byte [attempts]
    jnz retry
failure:
    mov ax,0xB800
    mov es,ax
    mov word [es:0],0x4F45
.halt:
    cli
    hlt
    jmp .halt
read_ok:
    cli
    in al,0x92
    or al,2
    and al,0xFE
    out 0x92,al
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
    cld
    mov edi,[destination]
    cmp edi,0x100000
    jne .copy
    ; Retain warm-boot BSS clearing, including older installed kernels.
    xor eax,eax
    mov ecx,0x100000
    rep stosd
    mov edi,0x100000
.copy:
    mov esi,0x8000
    mov ecx,1024
    rep movsd
    mov [destination],edi
    add dword [dap+8],8
    dec word [iterations]
    jnz .more
    mov esp,0x90000
    mov eax,0x2BADB002
    mov ebx,0x5000
    jmp 0x08:0x100000
.more:
    jmp 0x18:transition
bits 16
transition:
    mov eax,cr0
    and al,0xFE
    mov cr0,eax
    jmp 0:real
real:
    xor ax,ax
    mov ds,ax
    mov es,ax
    mov ss,ax
    mov sp,0x7C00
    sti
    jmp load
times 510-($-$$) db 0
dw 0xAA55
