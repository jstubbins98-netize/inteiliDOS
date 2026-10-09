; Synthetic DOS programs, built by run_dos_tests.py; no copyrighted binaries.
BITS 16
%macro ENABLE_TONE 0
    mov al, 0xB6
    out 0x43, al
    mov al, 4560&255
    out 0x42, al
    mov al, 4560>>8
    out 0x42, al
    mov al, 3
    out 0x61, al
%endmacro
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
    mov al, 0x11
    out 0x20, al                ; PIC reinitialization must never reach hardware
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
    ENABLE_TONE
    mov al, 0xFF
    out 0x21, al
    mov ah, 8
    int 0x21                   ; blocking input remains interruptible with F8
    mov ax, 0x4CEE
    int 0x21
%elif CASE = 8
    ENABLE_TONE
    mov al, 0xFF
    out 0x21, al                ; masks must not suppress host F8/keyboard IRQs
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
%elif CASE = 11
    mov ax, 0x1130
    xor bx, bx
    int 0x10
    cmp cx, 16
    jne fail_crt
    cmp dl, 24
    jne fail_crt
    mov al, [es:bp]             ; actual read-only BIOS ROM font data
    mov ax, 0x0200
    xor bx, bx
    mov dx, 0x091F
    int 0x10
    mov ax, 0x0040
    mov ds, ax
    cmp word [0x50], 0x091F     ; BIOS position agrees with the private BDA
    jne fail_crt
    mov dx, 0x3D4
    mov ax, 0x020E
    out dx, ax                 ; index + high cursor address, virtual only
    mov ax, 0xEF0F
    out dx, ax                 ; row 9, column 31 = 751 = 0x02EF
    in al, dx
    cmp al, 0x0F
    jne fail_crt
    inc dx
    mov ax, 0xA000
    in al, dx
    cmp ax, 0xA0EF             ; byte IN preserves AH
    jne fail_crt
    cmp word [0x50], 0x091F
    jne fail_crt
    mov dx, 0x3DA
    in al, dx
    mov bl, al
    in al, dx
    xor al, bl
    cmp al, 9                  ; virtual display status permits snow loops
    jne fail_crt
    mov ax, 0x4C2A
    int 0x21
fail_crt:
    mov ax, 0x4CEE
    int 0x21
%elif CASE = 12
    mov dx, 0x3D4
    xor al, al                 ; display timing is not an allowed register
    out dx, al
%elif CASE = 13
    cli
    in al, 0x21
    cmp al, 0xB8
    jne fail_hardware
    mov al, 0xFF
    out 0x21, al
    in al, 0x21
    cmp al, 0xFF
    jne fail_hardware
    mov ax, 0x2508
    mov dx, timer_hook
    int 0x21
    mov al, 0
    out 0x40, al
    mov al, 8
    out 0x40, al                ; 582 Hz guest timer, NOT the host PIT
    mov al, 0xFE
    out 0x21, al
    sti
.wait_timer:
    cmp word [cs:timer_events], 3
    jb .wait_timer
    cli
    ENABLE_TONE
    in al, 0x61
    and al, 3
    cmp al, 3
    jne fail_hardware
    mov ax, 0x1B00
    xor bx, bx
    mov di, state_buffer
    int 0x10
    cmp al, 0x1B
    jne fail_hardware
    cmp byte [state_buffer+4], 3
    jne fail_hardware
    cmp word [state_buffer+5], 80
    jne fail_hardware
    cmp byte [state_buffer+34], 25
    jne fail_hardware
    cmp word [state_buffer+35], 16
    jne fail_hardware
    les bx, [state_buffer]
    cmp byte [es:bx], 8         ; don't advertise unsupported graphics modes
    jne fail_hardware
    cmp word [es:bx+1], 0
    jne fail_hardware
    mov ax, 0x1A00
    int 0x10
    cmp ax, 0x1A1A
    jne fail_hardware
    cmp bx, 8
    jne fail_hardware
    mov ax, 0xEF00
    mov dx, 0xFFFF
    int 0x10
    cmp dx, 0xFFFF
    jne fail_hardware
    mov ax, 5
    int 0x2A
    cmp ah, 0
    jne fail_hardware
    mov ax, 0x4C2A
    int 0x21
fail_hardware:
    mov ax, 0x4CEE
    int 0x21
timer_hook:
    push ax
    inc word [cs:timer_events]
    mov al, 0x20
    out 0x20, al
    pop ax
    iret
timer_events: dw 0
state_buffer: times 64 db 0
%elif CASE = 14
    mov al, 0x36
    out 0x43, al
    mov al, 100
    out 0x40, al
    xor al, al
    out 0x40, al                ; reject timers above the 1000 Hz monitor limit
%elif CASE = 15
    mov ax, 0x2504
    mov dx, overflow_hook
    int 0x21
    mov [saved_sp], sp
    xor ax, ax                 ; OF clear: INTO must not call the handler
    stc
    into
    pushf
    pop bx
    and bx, 0xA41              ; IF/ZF/CF retained, OF clear
    cmp bx, 0x241
    jne fail_into
    cmp word [overflow_count], 0
    jne fail_into
    xor ax, ax
    stc
    ; The smoke host temporarily presents this faulting CLI as CEh to the
    ; decoder. It reproduces an emulator trapping INTO even with OF clear.
    db 0xFA, 0x90, 0x90
    pushf
    pop bx
    and bx, 0xA41
    cmp bx, 0x241
    jne fail_into
    cmp word [overflow_count], 0
    jne fail_into
    mov ax, 0x7FFF
    add ax, 1                  ; OF set: deliver the guest's INT 04h
overflow_instruction:
    into
overflow_return:
    cmp ax, 0x8000             ; the handler saved/restored general registers
    jne fail_into
    cmp sp, [saved_sp]
    jne fail_into
    cmp word [overflow_count], 1
    jne fail_into
    cmp word [overflow_ip], overflow_return
    jne fail_into
    mov ax, [overflow_flags]
    and ax, 0xA00              ; interrupt frame contains original OF and IF
    cmp ax, 0xA00
    jne fail_into
    cmp word [overflow_cs], 0x2000
    jne fail_into
    test word [handler_flags], 0x200
    jnz fail_into              ; virtual IF cleared during the handler
    mov ax, 0x4C2A
    int 0x21
fail_into:
    mov ax, 0x4CEE
    int 0x21
overflow_hook:
    push bp
    mov bp, sp
    push ax
    mov ax, [ss:bp+2]
    mov [cs:overflow_ip], ax
    mov ax, [ss:bp+4]
    mov [cs:overflow_cs], ax
    mov ax, [ss:bp+6]
    mov [cs:overflow_flags], ax
    pushf
    pop ax
    mov [cs:handler_flags], ax
    inc word [cs:overflow_count]
    and word [ss:bp+6], 0xF7FF ; guest handler explicitly clears overflow
    pop ax
    pop bp
    iret
saved_sp: dw 0
overflow_count: dw 0
overflow_ip: dw 0
overflow_cs: dw 0
overflow_flags: dw 0
handler_flags: dw 0
%elif CASE = 16
    mov ax, 0x7FFF
    add ax, 1
    into                       ; no INT 04h hook: report a real overflow error
    mov ax, 0x4CEE
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
