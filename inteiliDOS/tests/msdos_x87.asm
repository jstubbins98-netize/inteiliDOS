; Original DOS floating-point program and CPU regression fixture. MPL-2.0.
; nasm -f bin -DDOS=1 msdos_x87.asm -o X87TEST.COM
; No DOS binaries/libraries are bundled. Run under plain DOS (not EMM386).
bits 16
org 100h
%macro check 2
    mov word [7004h],%1
    %2 fail
%endmacro
start:
    push cs
    pop ds
    push cs
    pop es
    mov sp,0xfffc
    fninit
    fnstcw [cw]
    cmp word [cw],037fh
    check 1,jne
    mov eax,1
    cpuid
    cmp eax,400h
    check 2,jne
    cmp edx,1                    ; FPU only, no TSC/MMX/SSE
    check 3,jne
%ifdef DOS
    int 11h
    test ax,2                   ; BIOS equipment bit must agree with CPUID
    check 49,jz
%endif

    ; Solve x*x - 5*x + 6 = 0 with the quadratic formula in actual x87.
    fild word [five]
    fmul st0,st0
    fild word [six]
    fimul word [four]
    fsubp st1,st0
    fsqrt
    fld st0
    fiadd word [five]
    fidiv word [two]
    fistp word [result]
    cmp word [result],3
    check 4,jne
    fild word [five]
    fsubrp st1,st0
    fidiv word [two]
    fistp word [result]
    cmp word [result],2
    check 5,jne

    ; 80-bit precision must not silently become host double precision.
    fld1
    fld tword [tiny]
    faddp st1,st0
    fstp tword [extended]
    cmp dword [extended],1
    check 6,jne
    cmp dword [extended+4],80000000h
    check 7,jne
    cmp word [extended+8],3fffh
    check 8,jne
    ; Precision control = 24 bits, then restore extended precision.
    fldcw [cw24]
    fld1
    fld tword [tiny]
    faddp st1,st0
    fstp tword [extended]
    cmp dword [extended],0
    check 9,jne
    fnstsw ax
    test ax,20h
    check 10,jz
    fninit

    ; Four rounding modes and signed integer / real conversions.
    fld dword [twohalf]
    fist word [result]
    cmp word [result],2
    check 11,jne
    fldcw [cwup]
    fist word [result]
    cmp word [result],3
    check 12,jne
    fldcw [cwdown]
    fchs
    fist word [result]
    cmp word [result],-3
    check 13,jne
    fldcw [cwzero]
    fistp word [result]
    cmp word [result],-2
    check 14,jne
    fninit
    fild qword [largeint]
    fistp qword [wide]
    mov eax,[wide]
    cmp eax,[largeint]
    check 15,jne
    mov eax,[wide+4]
    cmp eax,[largeint+4]
    check 16,jne
    fld qword [doubleval]
    fstp dword [singleval]
    cmp dword [singleval],3fc00000h
    check 17,jne
    fbld [bcd]
    fbstp [bcdout]
    mov si,bcd
    mov di,bcdout
    mov cx,10
    repe cmpsb
    check 18,jne

    ; Stack operations, tags, both save-state/environment operand sizes.
    fninit
    fld1
    fldz
    fxch st1
    fnstenv [env16]
    cmp word [env16+4],4fffh      ; physical R6=normal, R7=zero
    check 19,jne
    fnsave [save16]
    fnstcw [cw]
    cmp word [cw],37fh
    check 20,jne
    fnstenv [env16]
    cmp word [env16+4],0ffffh
    check 21,jne
    frstor [save16]
    fstp dword [singleval]
    cmp dword [singleval],3f800000h
    check 22,jne
    fstp dword [singleval]
    cmp dword [singleval],0
    check 23,jne
    fld tword [rawval]
    o32 fnsave [save32]
    o32 frstor [save32]
    fstp tword [extended]
    mov si,rawval
    mov di,extended
    mov cx,10
    repe cmpsb
    check 24,jne
    fnstenv [env16]
    fldcw [cwup]
    fldenv [env16]
    fnstcw [cw]
    cmp word [cw],37fh
    check 25,jne

    ; Comparisons, unordered NaN, empty stack and masked stack overflow.
    fninit
    fld1
    fldz
    fcompp
    fnstsw ax
    and ax,4500h
    cmp ax,100h
    check 26,jne
    fld tword [qnan]
    fld1
    fucompp
    fnstsw ax
    and ax,4501h
    cmp ax,4500h
    check 27,jne
    fninit
    fstp dword [singleval]
    fnstsw ax
    and ax,0241h
    cmp ax,0041h
    check 28,jne
    cmp dword [singleval],0ffc00000h
    check 29,jne
    fninit
    mov cx,9
overflow:
    fld1
    loop overflow
    fnstsw ax
    and ax,0241h
    cmp ax,0241h
    check 30,jne

    ; Masked divide-by-zero and invalid arithmetic; sticky status/clear.
    fninit
    fld1
    fldz
    fdivp st1,st0
    fstp dword [singleval]
    cmp dword [singleval],7f800000h
    check 31,jne
    fnstsw ax
    test ax,4
    check 32,jz
    fnclex
    fldz
    fldz
    fdivp st1,st0
    fnstsw ax
    test ax,1
    check 33,jz
    fnclex
    fnstsw ax
    test ax,80ffh
    check 34,jnz

    ; Bounded integer-only transcendental functions used by DOS runtimes.
    fninit
    fldz
    fsincos
    fistp word [result]          ; cosine(0)=1
    cmp word [result],1
    check 35,jne
    fistp word [result]          ; sine(0)=0
    cmp word [result],0
    check 36,jne
    fld1
    fld1
    fscale
    fistp word [result]
    cmp word [result],2
    check 37,jne
    fld1
    fild word [two]
    fyl2x                       ; log2(2)=1
    fistp word [result]
    cmp word [result],1
    check 38,jne
    fninit
    fld1
    f2xm1
    fistp word [result]
    cmp word [result],1
    check 39,jne
    ; Denormal operands, overflow, underflow, signaling NaNs, invalid FIST.
    fninit
    fld dword [denormal]
    fnstsw ax
    test ax,2
    check 43,jz
    fstp st0
    fld tword [largest]
    fmul st0,st0
    fnstsw ax
    and ax,28h
    cmp ax,28h
    check 44,jne
    fninit
    fld tword [smallest]
    fmul st0,st0
    fnstsw ax
    and ax,30h
    cmp ax,30h
    check 45,jne
    fninit
    fld tword [snan]
    fld1
    faddp st1,st0
    fnstsw ax
    test ax,1
    check 46,jz
    fninit
    fld tword [largest]
    fistp word [result]
    cmp word [result],8000h
    check 47,jne
    fnstsw ax
    test ax,1
    check 48,jz
    ; FPREM quotient flags and nearest-even FPREM1 differ at 7 / 2.
    fninit
    fild word [two]
    fild word [seven]
    fprem
    fnstsw ax
    and ax,4700h
    cmp ax,4200h                ; quotient 3 => Q1=C3, Q0=C1
    check 50,jne
    fistp word [result]
    cmp word [result],1
    check 51,jne
    fild word [seven]
    fprem1
    fnstsw ax
    and ax,4700h
    cmp ax,0100h                ; nearest quotient 4 => Q2=C0
    check 52,jne
    fistp word [result]
    cmp word [result],-1
    check 53,jne
    fninit
    fld1
    fldz
    fpatan                      ; atan2(1,0): finite, no divide-by-zero
    fnstsw ax
    test ax,5
    check 54,jnz

    ; Deferred unmasked #MF at WAIT (NE=1); FNSTSW/FNCLEX bypass it.
    fninit
    xor ax,ax
    mov es,ax
    mov eax,[es:16*4]
    mov [oldmf],eax
    mov word [es:16*4],mfhandler
    mov ax,cs
    mov [es:16*4+2],ax
    mov byte [mfactive],1
    mov eax,cr0
    mov [oldcr0],eax
    or eax,20h
    mov cr0,eax
    fldcw [cwdiv]
    fld1
    fldz
    fdivp st1,st0
    fnstsw ax
    and ax,8084h
    cmp ax,8084h
    check 40,jne
    fwait
    cmp word [mfcount],1
    check 41,jne
    ; Unmasked failure must not have popped or changed operands.
    fnstsw ax
    and ax,3800h
    cmp ax,3000h
    check 42,jne
    mov eax,[oldcr0]
    mov cr0,eax
    mov eax,[oldmf]
    mov [es:16*4],eax
    mov byte [mfactive],0
%ifdef DOS
    ; Exercise the real native adapter's legacy FERR#/IRQ13 route too.
    mov eax,[es:75h*4]
    mov [oldirq13],eax
    mov word [es:75h*4],irq13handler
    mov ax,cs
    mov [es:75h*4+2],ax
    in al,21h
    mov [mastermask],al
    and al,0fbh
    out 21h,al
    in al,0a1h
    mov [slavemask],al
    and al,0dfh
    out 0a1h,al
    mov byte [irqactive],1
    mov eax,[oldcr0]
    and eax,0ffffffdfh
    mov cr0,eax
    fninit
    fldcw [cwdiv]
    fld1
    fldz
    fdivp st1,st0
    sti
    fwait
    cmp word [irqcount],1
    check 55,jne
    call restore_irq
%endif
    push cs
    pop es
    fninit
    mov dword [7000h],08748600dh
    mov word [7004h],0
%ifdef DOS
    mov dx,passmsg
    mov ah,9
    int 21h
    mov ax,4c00h
    int 21h
%else
    hlt
    jmp $
%endif
mfhandler:
    fnclex
    inc word [cs:mfcount]
    iret
%ifdef DOS
irq13handler:
    push ax
    fnclex
    inc word [cs:irqcount]
    xor al,al
    out 0f0h,al
    mov al,20h
    out 0a0h,al
    out 20h,al
    pop ax
    iret
restore_irq:
    fnclex
    mov eax,[oldcr0]
    mov cr0,eax
    mov eax,[oldirq13]
    mov [es:75h*4],eax
    mov al,[mastermask]
    out 21h,al
    mov al,[slavemask]
    out 0a1h,al
    mov byte [irqactive],0
    ret
%endif
fail:
%ifdef DOS
    cmp byte [irqactive],0
    je .mf
    call restore_irq
.mf:
%endif
    cmp byte [mfactive],0
    je .done
    fnclex
    mov eax,[oldcr0]
    mov cr0,eax
    mov eax,[oldmf]
    mov [es:16*4],eax
.done:
%ifdef DOS
    mov dx,failmsg
    mov ah,9
    int 21h
    mov ax,4c01h
    int 21h
%else
    mov dword [7000h],0badf00dh
    hlt
    jmp $
%endif

align 4
cw dw 0
cw24 dw 007fh
cwup dw 0b7fh
cwdown dw 077fh
cwzero dw 0f7fh
cwdiv dw 037bh
two dw 2
four dw 4
five dw 5
six dw 6
seven dw 7
result dw 0
mfcount dw 0
mfactive db 0
oldmf dd 0
oldcr0 dd 0
%ifdef DOS
oldirq13 dd 0
irqactive db 0
mastermask db 0
slavemask db 0
irqcount dw 0
%endif
largeint dq -12345678901234567
doubleval dq 1.5
twohalf dd 2.5
tiny dq 8000000000000000h
     dw 3fc0h                   ; 2**-63
rawval dq 8123456789abcdefh
       dw 4123h
qnan dq 0c000000000000001h
     dw 7fffh
snan dq 08000000000000001h
     dw 7fffh
largest dq 0ffffffffffffffffh
        dw 7ffeh
smallest dq 08000000000000000h
         dw 1
denormal dd 1
bcd db 78h,56h,34h,12h,90h,78h,56h,34h,12h,80h
bcdout times 10 db 0
singleval dd 0
wide dq 0
extended times 10 db 0
env16 times 14 db 0
save16 times 94 db 0
save32 times 108 db 0
passmsg db 'X87 QUADRATIC: roots 3 and 2',13,10,'X87 ARITHMETIC STACK EXCEPTIONS STATE PASS',13,10,'$'
failmsg db 'X87 TEST FAILED (test number at DS:7004)',13,10,'$'
