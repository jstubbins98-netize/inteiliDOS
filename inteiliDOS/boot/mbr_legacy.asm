; =============================================================================
; inteiliDOS -- boot/mbr_legacy.asm
; Legacy CHS Boot Sector — 486/Pentium / 440BX / El Torito Floppy-Emulation
; =============================================================================
;
; Assembled as a flat 512-byte binary:
;   nasm -f bin -o mbr_legacy.bin boot/mbr_legacy.asm
;
; DIFFERENCES FROM mbr.asm
; -------------------------
;   mbr.asm          uses INT 13h AH=0x42  (LBA48 Extended Read, 1996+)
;   mbr_legacy.asm   uses INT 13h AH=0x02  (CHS Read, supported since the XT)
;
; COMPATIBILITY TARGET
; --------------------
;   • Intel 486DX / 486DX2 / 486DX4 (1989–1994)
;   • Intel Pentium / Pentium MMX (1993–1997)
;   • Any BIOS from the mid-1990s that implements CHS reads via INT 13h
;   • El Torito floppy-emulation mode (1.44 MB, 80C/2H/18S geometry)
;   • HP Vectra VE and similar 440BX-era workstations
;
; DESIGN NOTES
; ------------
;   1.  Uses FS (NOT ES) as the 4 GB unreal-mode segment so that ES can
;       remain 0x0000 throughout the read loop.  INT 13h AH=0x02 requires
;       ES:BX to point to the destination buffer; if ES had the GDT limit
;       loaded the BIOS might compute the wrong base.  FS is not touched
;       by any real-mode BIOS call.
;
;   2.  Geometry is detected at run-time via INT 13h AH=0x08.  This covers
;       both real hard disks (variable CHS) and El Torito floppy emulation
;       (fixed 80/2/18).  Falls back to 63S/16H if AH=0x08 fails.
;
;   3.  One sector is read per INT 13h call into a 512-byte bounce buffer
;       at 0x7E00 (immediately after the MBR in low memory), then copied
;       dword-by-dword to FS:EDI = physical 0x100000.  This avoids any
;       64 KB DMA-boundary problem and matches mbr.asm's output exactly:
;       the kernel lands at 0x100000 and boot.asm's _start is called the
;       same way from both boot sectors.
;
;   4.  The KLBA patch area is present for compatibility with setup.c.
;       cur_lba is unused (CHS state is maintained separately), but rem_cnt
;       is live and can be patched at install time.
;
; =============================================================================

[BITS 16]
[ORG 0x7C00]

; ── Selectors ─────────────────────────────────────────────────────────────────
%define SEL_CODE32   0x08
%define SEL_DATA32   0x10

; ── Load parameters ───────────────────────────────────────────────────────────
%define KERN_LBA     1          ; first logical sector (unused in CHS, kept for setup.c)
%define KERN_SECTS   1024       ; sectors to load (512 KB); patched by setup.c via KLBA
%define KERN_DEST    0x100000   ; physical load address (matches linker.ld)
%define BOUNCE_BUF   0x7E00     ; 512-byte bounce buffer (sector after MBR)

; =============================================================================
; _start — BIOS entry.  DL = boot drive, CS:IP = 0x0000:0x7C00
; =============================================================================
_start:
    ; ── Step 1: Sanitise segments & stack ─────────────────────────────────────
    cli
    xor  ax, ax
    mov  ds, ax
    mov  es, ax
    mov  ss, ax
    mov  sp, 0x7BFE
    mov  [boot_drv], dl
    sti

    ; ── Step 2: A20 gate via Fast A20 (port 0x92, 440BX / compatible) ─────────
    in   al, 0x92
    test al, 0x02
    jnz  .a20_ok
    or   al, 0x02
    and  al, 0xFE           ; keep bit 0 clear — do NOT trigger system reset
    out  0x92, al
.a20_ok:

    ; ── Step 3: Print banner ────────────────────────────────────────────────
    mov  si, msg_boot
    call bios_print

    ; ── Step 4: Enter unreal mode — load FS with a 4 GB data descriptor ───────
    ;   FS is used for all high-memory writes (FS:EDI → physical KERN_DEST).
    ;   ES stays 0x0000 so INT 13h CHS reads can use ES:BX normally.
    lgdt [gdtr]
    cli
    mov  eax, cr0
    or   eax, 1
    mov  cr0, eax               ; PE = 1 (enter protected mode)
    mov  ax, SEL_DATA32
    mov  fs, ax                 ; load FS descriptor cache: base=0, limit=4 GB
    mov  eax, cr0
    and  eax, ~1
    mov  cr0, eax               ; PE = 0 (back to real mode — descriptor cache kept)
    jmp  0x0000:.unreal         ; flush instruction queue; reload CS as real-mode seg
.unreal:
    xor  ax, ax
    mov  ds, ax
    sti
    ; FS now has a 4 GB limit.  ES = 0x0000 (for INT 13h).

    ; ── Step 5: Detect drive geometry via INT 13h AH=0x08 ─────────────────────
    ;   Returns: CH = max cylinder (low 8), CL bits 7:6 = cyl bits 9:8
    ;            CL bits 5:0 = sectors per track (1-based maximum)
    ;            DH = max head index (number of heads − 1)
    ;   On failure: fall back to 63 sectors / 16 heads (typical ATA translation).
    push es                     ; some BIOSes trash ES — save it
    xor  di, di
    mov  es, di                 ; ES:DI = 0x0000:0x0000 (avoid BIOS bug)
    mov  ah, 0x08
    mov  dl, [boot_drv]
    int  0x13
    pop  es
    jc   .geo_default
    and  cl, 0x3F               ; isolate sectors-per-track (bits 5:0)
    mov  [max_sect], cl
    inc  dh                     ; DH = max head index → heads = DH+1
    mov  [max_head], dh
    jmp  .geo_done
.geo_default:
    mov  byte [max_sect], 63
    mov  byte [max_head], 16
.geo_done:

    ; ── Step 6: Initialise CHS state and destination pointer ──────────────────
    mov  byte [chs_cyl],  0
    mov  byte [chs_head], 0
    mov  byte [chs_sect], 2     ; sector 1 is the boot sector itself; start at 2
    mov  edi, KERN_DEST

; =============================================================================
; .read_loop — CHS disk read loop
; =============================================================================
.read_loop:
    cmp  word [rem_cnt], 0
    je   .load_done

    ; Build INT 13h AH=0x02 register state:
    ;   AL = 1 sector, CH = cylinder, CL = sector, DH = head
    ;   DL = drive, ES:BX = bounce buffer
    xor  ax, ax
    mov  ah, 0x02
    mov  al, 1
    mov  ch, [chs_cyl]
    mov  cl, [chs_sect]         ; bits 7:6 unused (< 256 cylinders assumed)
    mov  dh, [chs_head]
    mov  dl, [boot_drv]
    mov  bx, BOUNCE_BUF         ; ES is still 0x0000
    int  0x13
    jc   .disk_err

    ; ── Copy 512 bytes from bounce buffer (DS:SI) to FS:EDI ────────────────
    mov  si, BOUNCE_BUF
    mov  ecx, 128               ; 128 × 4 bytes = 512 bytes
.copy_dwords:
    mov  eax, [si]
    mov  [fs:edi], eax          ; FS.base=0, limit=4 GB → physical address = EDI
    add  si,  4
    add  edi, 4
    loop .copy_dwords

    dec  word [rem_cnt]

    ; ── Advance CHS to next sector ─────────────────────────────────────────
    mov  al, [chs_sect]
    inc  al
    cmp  al, [max_sect]
    jbe  .sect_ok               ; still within this track
    mov  al, 1                  ; wrap sector back to 1
    mov  ah, [chs_head]
    inc  ah
    cmp  ah, [max_head]
    jb   .head_ok               ; still within this cylinder
    xor  ah, ah                 ; wrap head to 0
    inc  byte [chs_cyl]         ; advance cylinder
.head_ok:
    mov  [chs_head], ah
.sect_ok:
    mov  [chs_sect], al

    jmp  .read_loop

; =============================================================================
; Kernel loaded — switch to 32-bit protected mode and hand off
; =============================================================================
.load_done:
    mov  si, msg_kern
    call bios_print

    cli
    lgdt [gdtr]
    mov  eax, cr0
    or   eax, 1
    mov  cr0, eax
    jmp  SEL_CODE32:.pm32       ; far jump: reload CS with 32-bit code descriptor

; =============================================================================
; 32-bit protected-mode entry — identical handoff to mbr.asm
; =============================================================================
[BITS 32]
.pm32:
    mov  ax, SEL_DATA32
    mov  ds, ax
    mov  es, ax
    mov  fs, ax
    mov  gs, ax
    mov  ss, ax
    mov  esp, 0x7BFC

    ; Debug stamp: bright 'L' on green at top-left VGA cell (L = Legacy path)
    mov  word [0xB8000], 0x2A4C ; 'L' (0x4C) | green-on-black attr (0x2A)

    ; Hand off to _start in boot/boot.asm at KERN_DEST:
    ;   EAX = 0 → MBR boot path (skips Multiboot memory map check)
    ;   EBX = 0 → no Multiboot info
    xor  eax, eax
    xor  ebx, ebx
    jmp  KERN_DEST              ; → _start at 0x100000 (does not return)

.hang:
    cli
    hlt
    jmp  .hang

; =============================================================================
; Error handler (16-bit)
; =============================================================================
[BITS 16]
.disk_err:
    mov  si, msg_err
    call bios_print
.halt:
    cli
    hlt
    jmp  .halt

; =============================================================================
; bios_print — print null-terminated string at DS:SI via INT 10h teletype
; =============================================================================
bios_print:
    pusha
.bp_loop:
    lodsb
    test al, al
    jz   .bp_done
    mov  ah, 0x0E
    xor  bh, bh
    int  0x10
    jmp  .bp_loop
.bp_done:
    popa
    ret

; =============================================================================
; Strings
; =============================================================================
msg_boot  db '[LEGACY] ', 0
msg_kern  db '[K]', 0x0D, 0x0A, 0
msg_err   db 'Disk error', 0x0D, 0x0A, 0

; =============================================================================
; Variables
; =============================================================================
boot_drv  db 0                  ; boot drive saved from DL at entry
chs_cyl   db 0                  ; current cylinder
chs_head  db 0                  ; current head
chs_sect  db 2                  ; current sector (1-based; starts at 2)
max_sect  db 18                 ; sectors per track (overwritten by INT 13h AH=08h)
max_head  db 2                  ; number of heads  (overwritten by INT 13h AH=08h)

; ── Install-time patch area (compatible with setup.c / KLBA convention) ───────
; setup.c scans for 'KLBA', skips cur_lba (dd), then patches rem_cnt (dw).
kern_patch_magic  db 0x4B, 0x4C, 0x42, 0x41   ; "KLBA" sentinel
cur_lba           dd KERN_LBA                   ; unused in CHS mode; kept for setup.c
rem_cnt           dw KERN_SECTS                 ; ← patched by setup.c / build system

; =============================================================================
; Global Descriptor Table (identical to mbr.asm)
; =============================================================================
align 8
gdt_base:
    dq 0x0000000000000000       ; null descriptor
    dq 0x00CF9A000000FFFF       ; 0x08  32-bit ring-0 code  (base=0, limit=4 GB)
    dq 0x00CF92000000FFFF       ; 0x10  32-bit ring-0 data  (base=0, limit=4 GB)
gdt_end:

gdtr:
    dw gdt_end - gdt_base - 1
    dd gdt_base

; =============================================================================
; Boot signature
; =============================================================================
    times 510 - ($ - $$) db 0
    dw 0xAA55
