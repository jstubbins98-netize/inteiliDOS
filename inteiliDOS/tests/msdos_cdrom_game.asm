; A real DOS game-shaped fixture: stream a >64 KiB resource from the CD,
; exercise seek, and save only to the private HDD.
bits 16
org 100h
    mov dx,resource
    mov ax,3d00h
    int 21h
    jc failed
    mov bx,ax
    xor bp,bp
    mov di,258             ; 258 * 512 = 132096 (> four 32 KiB clusters)
read:
    mov dx,buffer
    mov cx,512
    mov ah,3fh
    int 21h
    jc failed
    cmp ax,512
    jne failed
    mov si,buffer
    mov cx,512
check:
    lodsb
    cmp al,37
    jne failed
    loop check
    dec di
    jnz read
    mov ax,4200h
    xor cx,cx
    mov dx,32760
    int 21h
    jc failed
    mov dx,buffer
    mov cx,32
    mov ah,3fh
    int 21h
    jc failed
    cmp ax,32
    jne failed
    mov ah,3eh
    int 21h
    mov dx,save
    xor cx,cx
    mov ah,3ch
    int 21h
    jc failed
    mov bx,ax
    mov dx,success
    mov cx,success_end-success
    mov ah,40h
    int 21h
    jc failed
    mov ah,3eh
    int 21h
    mov dx,success
    mov ah,9
    int 21h
    mov ax,4c00h
    int 21h
failed:
    mov dx,failure
    mov ah,9
    int 21h
    mov ax,4c01h
    int 21h
resource db 'ASSET.DAT',0
save db 'C:\CDSAVE.TXT',0
success db 'CDGAME PASS',13,10
success_end:
    db '$'
failure db 'CDGAME FAIL',13,10,'$'
buffer times 512 db 0
