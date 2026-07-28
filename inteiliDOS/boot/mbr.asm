; =============================================================================
; inteiliDOS MBR Boot Sector
; =============================================================================
; Assembled as a flat 512-byte binary:
;   nasm -f bin -o mbr.bin boot/mbr.asm
;
; What it does:
;   1. Enables the A20 gate (fast method via port 0x92)
;   2. Enters "unreal mode" so ES has a 4 GB segment limit
;   3. Uses BIOS INT 13h extended reads (LBA48 DAP) to load
;      KERN_SECTS sectors starting at LBA KERN_LBA into physical
;      address KERN_DEST (0x100000, matching the kernel linker script)
;   4. Switches to 32-bit protected mode and calls kernel_main(0, 0)
;      — magic=0 skips the Multiboot memory map; everything else boots normally
; =============================================================================

[BITS 16]
[ORG 0x7C00]

; ── Constants ─────────────────────────────────────────────────────────────────
%define SEL_CODE32   0x08       ; GDT selector: 32-bit ring-0 code
%define SEL_DATA32   0x10       ; GDT selector: 32-bit ring-0 data
%define KERN_LBA     1          ; first LBA holding the kernel image
%define KERN_SECTS   1024       ; sectors to load (512 KB max); patched at install time
%define KERN_DEST    0x100000   ; physical load address (must match linker.ld)
%define DAP_BUF      0x7E00     ; low-memory bounce buffer (right after us)

; =============================================================================
; _start — entry point (CS=0, IP=0x7C00, DL=boot drive)
; =============================================================================
_start:
    cli
    xor  ax, ax
    mov  ds, ax
    mov  es, ax
    mov  ss, ax
    mov  sp, 0x7BFE
    mov  [boot_drv], dl
    sti

    ; ── A20 gate via port 0x92 (fast A20, supported by most BIOSes/VMs) ──────
    in   al, 0x92
    test al, 0x02
    jnz  .a20_ok
    or   al, 0x02
    and  al, 0xFE           ; keep bit 0 clear (avoid reset)
    out  0x92, al
.a20_ok:

    ; ── Load GDT for unreal mode ──────────────────────────────────────────────
    lgdt [gdtr]

    ; ── Enter PM briefly — load 4 GB data descriptor into ES ─────────────────
    cli
    mov  eax, cr0
    or   eax, 1
    mov  cr0, eax           ; PE=1

    ; Load the 4 GB data segment into ES while in protected mode
    mov  ax, SEL_DATA32
    mov  es, ax             ; ES.limit = 4 GB (persists after returning to RM)

    ; Clear PE bit — back to real mode
    mov  eax, cr0
    and  eax, ~1
    mov  cr0, eax           ; PE=0

    ; Far jump to flush the instruction queue and reload CS (real-mode base)
    jmp  0x0000:.unreal

.unreal:
    ; Now in "unreal mode": real-mode segmentation, but ES has a 4 GB limit.
    xor  ax, ax
    mov  ds, ax
    sti

    ; ── Debug: print "[MBR]" so we know the MBR reached the load stage ───────
    push ax
    push bx
    mov  ah, 0x0E
    xor  bh, bh
    mov  al, '['
    int  0x10
    mov  al, 'M'
    int  0x10
    mov  al, 'B'
    int  0x10
    mov  al, 'R'
    int  0x10
    mov  al, ']'
    int  0x10
    pop  bx
    pop  ax

    ; ── Load kernel sectors from disk into physical 0x100000 ─────────────────
    ; cur_lba and rem_cnt are pre-initialised in the data section;
    ; setup.c patches them at install time via the "KLBA" magic.
    mov  edi, KERN_DEST

.read_loop:
    ; Build Disk Address Packet (DAP) in-place
    mov  byte [dap],     0x10       ; struct size = 16 bytes
    mov  byte [dap+1],   0x00
    mov  word [dap+2],   1          ; transfer 1 sector per call
    mov  word [dap+4],   DAP_BUF   ; buffer offset
    mov  word [dap+6],   0x0000     ; buffer segment
    mov  eax,  [cur_lba]
    mov  [dap+8],  eax              ; LBA bits 0–31
    mov  dword [dap+12], 0          ; LBA bits 32–63

    ; INT 13h/AH=42h — extended read
    mov  ah,  0x42
    mov  dl,  [boot_drv]
    mov  si,  dap
    int  0x13
    jc   .disk_err

    ; Copy 512 bytes from DAP_BUF (DS:SI, real-mode) to ES:EDI (4 GB segment)
    ; In [BITS 16]: using 32-bit registers triggers the operand/address-size
    ; prefix automatically in NASM.
    mov  si,  DAP_BUF
    mov  ecx, 128                   ; 512 / 4 = 128 dwords

.copy_dwords:
    mov  eax,  [si]                 ; load 4 bytes from low memory  (DS:SI)
    mov  [es:edi], eax              ; store to ES:EDI (4 GB segment)
    add  si,  4
    add  edi, 4
    loop .copy_dwords

    inc  dword [cur_lba]
    dec  word  [rem_cnt]
    jnz  .read_loop

    ; ── Debug: print "[K]" — kernel sectors loaded ───────────────────────────
    push ax
    push bx
    mov  ah, 0x0E
    xor  bh, bh
    mov  al, '['
    int  0x10
    mov  al, 'K'
    int  0x10
    mov  al, ']'
    int  0x10
    pop  bx
    pop  ax

    ; ── Enter 32-bit protected mode for the last time ─────────────────────────
    cli
    lgdt [gdtr]
    mov  eax, cr0
    or   eax, 1
    mov  cr0, eax
    jmp  SEL_CODE32:.pm32           ; far jump: reload CS with 32-bit descriptor

; =============================================================================
; 32-bit protected-mode entry — hand off to the kernel
; =============================================================================
[BITS 32]
.pm32:
    mov  ax, SEL_DATA32
    mov  ds, ax
    mov  es, ax
    mov  fs, ax
    mov  gs, ax
    mov  ss, ax
    mov  esp, 0x7BFC                ; stack just below the MBR load address

    ; ── Debug: stamp top-left VGA cell with bright 'M' on green ─────────────
    ; This appears briefly before the kernel's vga_init() clears the screen.
    ; If you see it persist, the kernel crashed before reaching vga_clear().
    mov  word [0xB8000], 0x2A4D     ; 'M' (0x4D) | green-on-black attr (0x2A)

    ; Hand off to the kernel entry point (_start in boot/boot.asm).
    ; _start reads magic and info from registers (same convention as GRUB):
    ;   eax = magic (0 → MBR boot path, skips Multiboot memory map)
    ;   ebx = Multiboot info pointer (0 → none)
    xor  eax, eax                   ; magic = 0 (MBR boot)
    xor  ebx, ebx                   ; no Multiboot info
    jmp  KERN_DEST                  ; → _start at 0x100000 (does not return)

.hang:
    cli
    hlt
    jmp  .hang

; =============================================================================
; Error handler (16-bit, only reached before kernel handoff)
; =============================================================================
[BITS 16]
.disk_err:
    mov  si, msg_err
.puts:
    lodsb
    test al, al
    jz   .halt
    mov  ah, 0x0E
    xor  bh, bh
    int  0x10
    jmp  .puts
.halt:
    cli
    hlt
    jmp  .halt

; ── Strings ───────────────────────────────────────────────────────────────────
msg_err   db 'inteiliDOS: disk read error — check your installation', 0x0D, 0x0A, 0

; ── Variables / install-time patch area ───────────────────────────────────────
; The 4-byte "KLBA" sentinel lets setup.c locate cur_lba and rem_cnt in the
; assembled binary and patch them with the real install LBA/sector-count before
; writing the MBR to disk.  Do NOT reorder or add bytes between the sentinel
; and these two fields without updating setup.c.
kern_patch_magic  db 0x4B, 0x4C, 0x42, 0x41   ; "KLBA"
cur_lba           dd KERN_LBA                   ; ← patched by setup.c
rem_cnt           dw KERN_SECTS                 ; ← patched by setup.c
boot_drv          db 0

; ── Disk Address Packet (16 bytes) ────────────────────────────────────────────
align 4
dap:
    times 16 db 0

; ── Global Descriptor Table ───────────────────────────────────────────────────
; Three entries × 8 bytes = 24 bytes
align 8
gdt_base:
    dq 0x0000000000000000       ; 0x00  null descriptor
    dq 0x00CF9A000000FFFF       ; 0x08  32-bit ring-0 code  (base=0, limit=4 GB)
    dq 0x00CF92000000FFFF       ; 0x10  32-bit ring-0 data  (base=0, limit=4 GB)
gdt_end:

gdtr:
    dw gdt_end - gdt_base - 1  ; limit
    dd gdt_base                 ; base (physical address within first 640 KB)

; ── Padding + MBR boot signature ──────────────────────────────────────────────
    times 510 - ($ - $$) db 0
    dw 0xAA55
