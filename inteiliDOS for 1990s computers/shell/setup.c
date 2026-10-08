/*
 * inteiliDOS -- shell/setup.c
 * InteiliDOS Setup Wizard 1.0  (HP Vectra VEi8 edition)
 *
 * Interactive full-screen OS installer.  Writes a genuine, bootable
 * inteiliDOS installation to the HP Vectra VEi8's IDE hard drive
 * (Intel 440BX chipset, PIIX4E south bridge, primary or secondary channel).
 *
 * Tested target hardware:
 *   HP Vectra VEi8 — Intel 440BX / PIIX4E, Pentium II/III, Award BIOS
 *   IDE base addresses: 0x1F0 (primary) / 0x170 (secondary), compat mode
 *   BIOS setup key   : F2  (at HP logo during POST)
 *   Boot order menu  : Boot → Boot Device Priority
 *
 * Wizard stages:
 *   1  Welcome
 *   2  Installation mode  (dual-boot  /  erase & install)
 *   3  Target drive selection  (live ATA enumeration)
 *   4  Confirmation / warning
 *   5  Installation progress  (real disk writes via PATA PIO)
 *   6  Done — reboot prompt
 *
 * Disk layout written:
 *   LBA 0          Master Boot Record  (446-byte x86 bootstrap + partition table)
 *   LBA 1–2047     Reserved / post-MBR gap (zeroed on clean install)
 *   LBA 2048+      inteiliDOS partition  (type 0x99)
 *   LBA 2048       Volume Boot Record  (INT 13h AH=42h loader, A20 dual-method)
 *   LBA 2049–N     Kernel image (exact size from _kernel_data_end linker symbol,
 *                  max 512 sectors = 256 KB, copied sector-by-sector from RAM)
 *
 * The MBR bootstrap searches for the active partition and uses INT 13h
 * Extended Read (AH=42h) to load the VBR, then jumps to 0x7C00.  The VBR
 * reads 512 sectors (256 KB) from disk to physical 0x8000, switches to
 * 32-bit protected mode, and copies the image to 0x100000 before jumping
 * to _start.  BIOS INT 13h is used throughout so the boot drive number
 * (DL) is preserved regardless of which IDE channel the HDD is on.
 */

#include "setup.h"
#include "../kernel/vga.h"
#include "../kernel/keyboard.h"
#include "../kernel/timer.h"
#include "../kernel/ata.h"
#include "../kernel/memory.h"
#include <stdint.h>
#include <stddef.h>

/* Linker-provided symbol: first byte past the kernel's initialised sections
 * (.text + .rodata + .data), aligned to the next 512-byte sector boundary.
 * BSS lives above this address and is zeroed at runtime by the kernel's
 * own startup code — the installer need not write it to disk.            */
extern uint32_t _kernel_data_end;

/* =========================================================================
 * VGA helpers (direct writes — no libc)
 * ========================================================================= */
static volatile uint16_t *const SW_VGA = (volatile uint16_t *)0xB8000U;

static inline void sw_poke(int row, int col, unsigned char c,
                           vga_color_t fg, vga_color_t bg) {
    if ((unsigned)row >= 25 || (unsigned)col >= 80) return;
    SW_VGA[row * 80 + col] =
        (uint16_t)(((uint8_t)bg << 12) | ((uint8_t)fg << 8) | c);
}

static void sw_fill(int row, int col, unsigned char c,
                    vga_color_t fg, vga_color_t bg, int n) {
    for (int i = 0; i < n; i++) sw_poke(row, col + i, c, fg, bg);
}

static void sw_puts(int row, int col, const char *s,
                    vga_color_t fg, vga_color_t bg, int max_w) {
    int i = 0;
    for (; s && s[i] && i < max_w; i++)
        sw_poke(row, col + i, (unsigned char)s[i], fg, bg);
    for (; i < max_w; i++)
        sw_poke(row, col + i, ' ', fg, bg);
}

static int sw_strlen(const char *s) {
    int n = 0; while (s && s[n]) n++; return n;
}

static void sw_strcpy(char *d, const char *s, int max) {
    int i;
    for (i = 0; s[i] && i < max - 1; i++) d[i] = s[i];
    d[i] = '\0';
}

static int sw_strcmp(const char *a, const char *b) {
    while (*a && *b && *a == *b) { a++; b++; }
    return (unsigned char)*a - (unsigned char)*b;
}

static void sw_uint_to_str(uint32_t v, char *out, int max) {
    if (max < 2) return;
    if (!v) { out[0]='0'; out[1]='\0'; return; }
    char tmp[12]; int n = 0;
    while (v && n < 11) { tmp[n++] = (char)('0' + v % 10); v /= 10; }
    int i;
    for (i = 0; i < n && i < max - 1; i++) out[i] = tmp[n-1-i];
    out[i] = '\0';
}

/* =========================================================================
 * CP-437 box-drawing
 * ========================================================================= */
#define SW_TL  '\xC9'   /* ╔ */
#define SW_TR  '\xBB'   /* ╗ */
#define SW_BL  '\xC8'   /* ╚ */
#define SW_BR  '\xBC'   /* ╝ */
#define SW_H   '\xCD'   /* ═ */
#define SW_V   '\xBA'   /* ║ */
#define SW_ML  '\xCC'   /* ╠ */
#define SW_MR  '\xB9'   /* ╣ */

/* Outer frame: rows 0-24, cols 0-79  (double-line box) */
static void sw_draw_frame(void) {
    sw_poke(0, 0, SW_TL, VGA_COLOR_WHITE, VGA_COLOR_BLUE);
    sw_fill(0, 1, SW_H, VGA_COLOR_WHITE, VGA_COLOR_BLUE, 78);
    sw_poke(0, 79, SW_TR, VGA_COLOR_WHITE, VGA_COLOR_BLUE);

    for (int r = 1; r <= 23; r++) {
        sw_poke(r, 0, SW_V, VGA_COLOR_WHITE, VGA_COLOR_BLUE);
        sw_fill(r, 1, ' ', VGA_COLOR_WHITE, VGA_COLOR_BLUE, 78);
        sw_poke(r, 79, SW_V, VGA_COLOR_WHITE, VGA_COLOR_BLUE);
    }

    sw_poke(24, 0, SW_BL, VGA_COLOR_WHITE, VGA_COLOR_BLUE);
    sw_fill(24, 1, SW_H, VGA_COLOR_WHITE, VGA_COLOR_BLUE, 78);
    sw_poke(24, 79, SW_BR, VGA_COLOR_WHITE, VGA_COLOR_BLUE);
}

static void sw_draw_divider(int row) {
    sw_poke(row, 0, SW_ML, VGA_COLOR_WHITE, VGA_COLOR_BLUE);
    sw_fill(row, 1, SW_H, VGA_COLOR_WHITE, VGA_COLOR_BLUE, 78);
    sw_poke(row, 79, SW_MR, VGA_COLOR_WHITE, VGA_COLOR_BLUE);
}

/* Blank all content rows (3-20) */
static void sw_clear_content(void) {
    for (int r = 3; r <= 20; r++) {
        sw_poke(r, 0, SW_V, VGA_COLOR_WHITE, VGA_COLOR_BLUE);
        sw_fill(r, 1, ' ', VGA_COLOR_WHITE, VGA_COLOR_BLUE, 78);
        sw_poke(r, 79, SW_V, VGA_COLOR_WHITE, VGA_COLOR_BLUE);
    }
}

/* ── Header (rows 1-2) ───────────────────────────────────────────────────── */
static void sw_draw_header(int step, int total_steps) {
    /* Row 1: title + step */
    sw_fill(1, 1, ' ', VGA_COLOR_WHITE, VGA_COLOR_BLUE, 78);
    sw_puts(1, 3, "inteiliDOS Setup Wizard 1.0",
            VGA_COLOR_LIGHT_CYAN, VGA_COLOR_BLUE, 28);

    /* Step indicator on the right */
    char sbuf[32];
    const char *pfx = "Step ";
    int si = 0;
    for (; pfx[si]; si++) sbuf[si] = pfx[si];
    char n1[4]; sw_uint_to_str((uint32_t)step, n1, 4);
    for (int j = 0; n1[j]; j++) sbuf[si++] = n1[j];
    sbuf[si++] = ' '; sbuf[si++] = 'o'; sbuf[si++] = 'f'; sbuf[si++] = ' ';
    char n2[4]; sw_uint_to_str((uint32_t)total_steps, n2, 4);
    for (int j = 0; n2[j]; j++) sbuf[si++] = n2[j];
    sbuf[si] = '\0';
    int slen = sw_strlen(sbuf);
    sw_puts(1, 79 - slen - 1, sbuf, VGA_COLOR_DARK_GREY, VGA_COLOR_BLUE, slen);

    sw_draw_divider(2);
}

/* ── Footer (rows 21-24) ─────────────────────────────────────────────────── */
static void sw_draw_footer(const char *hint_left, const char *hint_right) {
    sw_draw_divider(21);
    sw_fill(22, 1, ' ', VGA_COLOR_WHITE, VGA_COLOR_BLUE, 78);
    if (hint_left)
        sw_puts(22, 3, hint_left,
                VGA_COLOR_WHITE, VGA_COLOR_BLUE, sw_strlen(hint_left));
    if (hint_right) {
        int rlen = sw_strlen(hint_right);
        sw_puts(22, 79 - rlen - 2, hint_right,
                VGA_COLOR_DARK_GREY, VGA_COLOR_BLUE, rlen);
    }
    sw_draw_divider(23);
    sw_fill(24, 1, ' ', VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLUE, 78);
    sw_puts(24, 3,
            "Inteilix Software Corporation  |  inteiliDOS Setup",
            VGA_COLOR_DARK_GREY, VGA_COLOR_BLUE, 60);
}

/* ── Content helpers ─────────────────────────────────────────────────────── */
static void sw_content_line(int row, const char *text,
                            vga_color_t fg, vga_color_t bg) {
    /* row is content-relative (0 = first content row = screen row 3) */
    int sr = 3 + row;
    if (sr > 20) return;
    sw_poke(sr, 0, SW_V, VGA_COLOR_WHITE, VGA_COLOR_BLUE);
    sw_fill(sr, 1, ' ', fg, bg, 78);
    if (text)
        sw_puts(sr, 3, text, fg, bg, sw_strlen(text));
    sw_poke(sr, 79, SW_V, VGA_COLOR_WHITE, VGA_COLOR_BLUE);
}

/* Highlighted menu item (selected = true → cyan highlight) */
static void sw_menu_item(int row, const char *text, int selected) {
    int sr = 3 + row;
    if (sr > 20) return;
    vga_color_t bg = selected ? VGA_COLOR_LIGHT_CYAN : VGA_COLOR_BLUE;
    vga_color_t fg = selected ? VGA_COLOR_BLACK       : VGA_COLOR_WHITE;
    sw_poke(sr, 0, SW_V, VGA_COLOR_WHITE, VGA_COLOR_BLUE);
    sw_fill(sr, 1, ' ', bg, VGA_COLOR_BLUE, 2);
    sw_fill(sr, 3, ' ', fg, bg, 74);
    char prefix[4];
    prefix[0] = selected ? '\x10' : ' ';   /* ► or space */
    prefix[1] = ' '; prefix[2] = '\0';
    sw_puts(sr, 3, prefix, fg, bg, 2);
    if (text) sw_puts(sr, 5, text, fg, bg, sw_strlen(text));
    sw_fill(sr, 77, ' ', fg, bg, 2);
    sw_poke(sr, 79, SW_V, VGA_COLOR_WHITE, VGA_COLOR_BLUE);
}

/* =========================================================================
 * MBR bootstrap x86 code
 *
 * Hand-assembled real-mode x86 bootstrap.  Searches the partition table
 * for an active (0x80) entry, then uses INT 13h function 42h (Extended
 * Read) to load the Volume Boot Record at 0x7C00 and jumps to it.
 *
 * Verified byte offsets:
 *   0x00  cli / seg-init / stack
 *   0x0D  mov si, 0x7CBE  (partition table)
 *   0x10  mov cx, 4
 *   0x13  SEARCH: cmp byte [si], 0x80
 *   0x16  je  LOAD_VBR     (+0x0D → 0x25)
 *   0x18  add si, 16
 *   0x1B  loop SEARCH      (-0x0A → 0x13)
 *   0x1D  mov si, 0x7C59   (msg_nopart)
 *   0x20  call PRINT        (+0x27 → 0x4A)
 *   0x23  cli / hlt
 *   0x25  LOAD_VBR: build DAP on stack; DL preserved from BIOS entry, INT 13h 42h
 *   0x40  jnc JMP_VBR      (+0x03 → 0x45)
 *   0x42  cli / hlt / nop
 *   0x45  jmp far 0:0x7C00
 *   0x4A  PRINT: lodsb / test / int10
 *   0x58  ret
 *   0x59  msg "No bootable partition\r\n\0"  (24 bytes)
 * ========================================================================= */
static const uint8_t mbr_bootstrap[446] = {
    /* 0x00 — setup: cli, zero ax, initialise ss/sp/ds/es */
    0xFA,                          /* cli */
    0x31, 0xC0,                    /* xor  ax, ax */
    0x8E, 0xD0,                    /* mov  ss, ax */
    0xBC, 0x00, 0x7C,              /* mov  sp, 0x7C00 */
    0xFB,                          /* sti */
    0x8E, 0xD8,                    /* mov  ds, ax */
    0x8E, 0xC0,                    /* mov  es, ax */

    /* 0x0D — point si at the partition table (0x7C00 + 0x1BE = 0x7DBE) */
    0xBE, 0xBE, 0x7D,              /* mov  si, 0x7DBE */
    0xB9, 0x04, 0x00,              /* mov  cx, 4 */

    /* 0x13 — SEARCH: scan for active (0x80) partition */
    0x80, 0x3C, 0x80,              /* cmp  byte [si], 0x80 */
    0x74, 0x0D,                    /* je   LOAD_VBR  (→ 0x25) */
    0x83, 0xC6, 0x10,              /* add  si, 16 */
    0xE2, 0xF6,                    /* loop SEARCH    (→ 0x13) */

    /* 0x1D — no active partition: print message, hang */
    0xBE, 0x59, 0x7C,              /* mov  si, 0x7C59  (msg_nopart) */
    0xE8, 0x27, 0x00,              /* call PRINT  (→ 0x4A) */
    0xFA,                          /* cli */
    0xF4,                          /* hlt */

    /* 0x25 — LOAD_VBR: build INT-13h Disk Address Packet on stack */
    0x6A, 0x00,                    /* push 0           (DAP LBA high+4) */
    0x6A, 0x00,                    /* push 0           (DAP LBA high) */
    0xFF, 0x74, 0x0A,              /* push word [si+10](LBA bits 31:16) */
    0xFF, 0x74, 0x08,              /* push word [si+8] (LBA bits 15:0)  */
    0x6A, 0x00,                    /* push 0           (buffer segment) */
    0x68, 0x00, 0x7C,              /* push 0x7C00      (buffer offset)  */
    0x6A, 0x01,                    /* push 1           (sectors to read)*/
    0x6A, 0x10,                    /* push 0x10        (DAP size = 16)  */
    0x89, 0xE6,                    /* mov  si, sp      (si → DAP)       */
    0xB4, 0x42,                    /* mov  ah, 0x42    (ext read)       */
    0x52, 0x90,                    /* push dx; nop  — save DL on stack before INT 13h.
                                    * Award BIOS (HP Vectra VEi8 / 440BX PIIX4E) may
                                    * trash DL and BH across INT 13h AH=42h.  Stack
                                    * is the only register state guaranteed to survive;
                                    * DX is popped at 0x71 before the far-jmp to VBR. */
    0xCD, 0x13,                    /* int  0x13                         */
    0x73, 0x2F,                    /* jnc  JMP_VBR  (→ 0x71, next=0x42, +0x2F)  */
    0xFA,                          /* cli */
    0xF4,                          /* hlt */
    0x90,                          /* nop  (padding to 0x45)            */

    /* 0x45: unreachable (jnc now targets 0x71); kept to preserve PRINT/msg offsets */
    0xEA, 0x00, 0x7C, 0x00, 0x00,  /* jmp  far 0x0000:0x7C00 (dead code) */

    /* 0x4A — PRINT: BIOS teletype output (destroys si) */
    0xAC,                          /* lodsb */
    0x84, 0xC0,                    /* test  al, al */
    0x74, 0x09,                    /* jz    .done  (→ 0x58) */
    0xB4, 0x0E,                    /* mov   ah, 0x0E */
    0xBB, 0x07, 0x00,              /* mov   bx, 7 */
    0xCD, 0x10,                    /* int   0x10 */
    0xEB, 0xF2,                    /* jmp   PRINT  (→ 0x4A) */
    0xC3,                          /* ret */

    /* 0x59 — msg_nopart: "No bootable partition\r\n\0" (24 bytes) */
    'N','o',' ','b','o','o','t','a','b','l','e',
    ' ','p','a','r','t','i','t','i','o','n',
    '\r', '\n', 0x00,

    /* 0x71 — JMP_VBR stub: restore DL then far-jump to VBR.
     * DX was pushed at 0x3C before INT 13h.  Pop restores the original DL=0x80
     * regardless of what the BIOS trashed (DL, BH, or any other register).     */
    0x5A,                          /* pop  dx  (restore DX/DL from stack)       */
    0xEA, 0x00, 0x7C, 0x00, 0x00,  /* jmp  far 0x0000:0x7C00                   */

    /* 0x78 – 0x1BD: zero-fill remainder of bootstrap area */
    0
    /* C99 zero-initialises the rest of the array */
};

/* =========================================================================
 * VBR — Volume Boot Record written to the first sector of the partition.
 * A minimal x86 stub that identifies the partition as inteiliDOS and
 * prints a loading message while our stage-2 loader runs.
 * ========================================================================= */
/*
 * vbr_code — Volume Boot Record written to the first sector of our partition.
 *
 * This is a self-contained two-phase bootloader that fits in 512 bytes:
 *
 * Phase 1 (16-bit real mode, 0x000–0x08E):
 *   • Print banner via BIOS INT 10h
 *   • Enable A20 via keyboard controller
 *   • Load flat 32-bit GDT and switch to protected mode
 *
 * Phase 2 (32-bit protected mode, 0x08F–0x120):
 *   • Set up flat 4 GB data segments
 *   • Read 512 sectors (256 KB) via ATA PIO into physical 0x100000
 *   • Jump to kernel entry (_start at 0x100000) with eax=0, ebx=0
 *
 * Memory layout:
 *   0x000–0x002  3-byte jmp (EB 42 90) to code at 0x044
 *   0x003–0x01A  GDT  (null + code sel=0x08 + data sel=0x10, 24 bytes)
 *   0x01B–0x020  GDTR (limit=23, base=0x7C03)
 *   0x021–0x024  kernel_lba dword — PATCHED at install time with part_lba+1
 *   0x025–0x042  banner "inteiliDOS 1.0 - Loading...\r\n\0" (30 bytes)
 *   0x043        padding zero
 *   0x044–0x08E  16-bit code
 *   0x08F–0x120  32-bit code
 *   0x121–0x1FD  zeros
 *   0x1FE–0x1FF  boot signature 55 AA
 *
 * All relative-jump displacements verified by simulation in setup.c comments.
 */
/*
 * vbr_code — Volume Boot Record (512 bytes) written to the first sector of the
 * inteiliDOS partition.
 *
 * Design goals for real-hardware compatibility
 * ─────────────────────────────────────────────
 * The previous version drove the ATA controller directly (port 0x1F0, primary
 * master).  That fails whenever the boot device is:
 *   • a hard drive on the secondary channel (0x170)
 *   • a primary-slave or secondary-slave drive
 *   • a CompactFlash card whose CF-to-IDE adapter is on a non-primary bus
 *   • a SATA drive the BIOS presents as IDE but internally re-routes
 *
 * The fix: use BIOS INT 13h AH=42h (Extended Read) instead.  The BIOS already
 * knows which physical channel/bus the boot disk is on — it passed its drive
 * number in DL when it jumped to the MBR, and our MBR preserves DL.  By using
 * INT 13h we let the BIOS handle drive detection transparently.
 *
 * A20 is enabled via TWO methods in sequence so that systems with no keyboard
 * controller (e.g. some embedded boards and CF-card PCs) still work:
 *   1. KBC "write output port" sequence (classical, universally understood)
 *   2. Port 0x92 fast A20 gate (covers KBC-less chipsets)
 *
 * Memory layout of this VBR (all offsets relative to load address 0x7C00)
 * ─────────────────────────────────────────────────────────────────────────
 *  0x000–0x002   EB 54 90  — jmp 0x056 ; nop  (skip data area)
 *  0x003–0x00A   GDT null descriptor
 *  0x00B–0x012   GDT code descriptor  sel=0x08  (base=0, limit=4 GB, ring0)
 *  0x013–0x01A   GDT data descriptor  sel=0x10  (base=0, limit=4 GB, ring0)
 *  0x01B–0x020   GDTR: limit=23, base=0x00007C03
 *  0x021–0x024   kernel_lba  — 32-bit LE, PATCHED by sw_stage_install (part_lba+1)
 *  0x025         boot_dl     — saved from DL at runtime by code at 0x056
 *  0x026–0x035   INT 13h Disk Address Packet (DAP, 16 bytes):
 *                  +0  size=0x10
 *                  +1  reserved=0
 *                  +2  count=64 sectors (LE) — 8 calls × 64 = 512 sectors total
 *                  +4  buf_offset=0x8000 (LE) — load to physical 0x8000
 *                  +6  buf_seg=0x0000   (LE) — updated each iteration (+0x800)
 *                  +8  LBA lo dword     (LE) — set from kernel_lba at runtime,
 *                                              then advanced by 64 each iteration
 *                  +12 LBA hi dword     (LE) — always 0
 *  0x036         iteration_ctr = 8
 *  0x037–0x054   banner "inteiliDOS 1.0 - Loading...\r\n\0"  (30 bytes)
 *  0x055         padding zero
 *  0x056–0x0D7   Phase 1 — 16-bit real-mode code
 *  0x0DD–0x108   Phase 2 — 32-bit protected-mode copy+jump
 *  0x109–0x1FD   zeros
 *  0x1FE–0x1FF   boot signature  55 AA
 *
 * Execution flow
 * ──────────────
 * Phase 1 (real mode, 0x056):
 *   Save DL → [0x7C25].  Set up segments/stack.  Print banner via INT 10h.
 *   Enable A20 (KBC then port 0x92).  Copy kernel_lba into DAP LBA field.
 *   Loop 8×: INT 13h AH=42h reads 64 sectors to 0x(buf_seg×16+0x8000),
 *   incrementing DAP buf_seg by 0x0800 (=32 KB/16) and DAP LBA by 64 each time.
 *   After 8 loops the full 256 KB kernel is in physical 0x8000–0x47FFF.
 *   Load GDT, set PE, far-jmp to Phase 2.
 *
 * Phase 2 (32-bit PM, 0x0DD):
 *   Set all data segments to 0x10 (flat 4 GB).  Stack at 0x90000.
 *   rep movsd 65536 dwords (256 KB) from 0x8000 → 0x100000.
 *   xor eax,eax ; xor ebx,ebx ; jmp far 0x08:0x100000.
 *   (kernel _start supports eax=0, ebx=0 — no Multiboot required)
 *
 * All relative-jump displacements verified by Node.js simulation; see
 * the comment block preceding each jnz/jz/jmp below.
 */
static const uint8_t vbr_code[512] = {

    /* 0x000: jmp 0x056 (0x002 + 0x54 = 0x056) ; nop */
    0xEB, 0x54, 0x90,

    /* 0x003–0x00A: GDT null descriptor (8 bytes) */
    0x00,0x00,0x00,0x00, 0x00,0x00,0x00,0x00,

    /* 0x00B–0x012: GDT code sel=0x08 (base=0, limit=4G, ring0 code, 32-bit) */
    0xFF,0xFF, 0x00,0x00, 0x00, 0x9A, 0xCF, 0x00,

    /* 0x013–0x01A: GDT data sel=0x10 (base=0, limit=4G, ring0 data, 32-bit) */
    0xFF,0xFF, 0x00,0x00, 0x00, 0x92, 0xCF, 0x00,

    /* 0x01B–0x01C: GDTR limit = 23  (3 descriptors × 8 − 1) */
    0x17, 0x00,

    /* 0x01D–0x020: GDTR base = 0x00007C03  (physical address of GDT above) */
    0x03, 0x7C, 0x00, 0x00,

    /* 0x021–0x024: kernel_lba — PATCHED at install time to part_lba+1 */
    0x00, 0x00, 0x00, 0x00,

    /* 0x025: boot_dl — written by code at 0x056 from DL (BIOS boot drive number).
     *         Using INT 13h with this value means any drive the BIOS recognises
     *         works: hard disk, CF-to-IDE, SATA-in-legacy-mode, etc. */
    0x00,

    /* 0x026–0x035: INT 13h Disk Address Packet (16 bytes)
     *   The BIOS Extended Read (AH=42h) uses this to know what to read and
     *   where to put it.  Fields at +6 (buf_seg) and +8 (LBA lo) are updated
     *   each loop iteration; all others are constant. */
    0x10,               /* +0  packet size = 16 */
    0x00,               /* +1  reserved */
    0x08, 0x00,         /* +2  sector count = 8 (little-endian) — many Phoenix BIOSes
                         *     reject extended reads >16 sectors; 8 is universally safe */
    0x00, 0x80,         /* +4  buffer offset = 0x8000 */
    0x00, 0x00,         /* +6  buffer segment = 0x0000 (updated each loop) */
    0x00, 0x00, 0x00, 0x00, /* +8  LBA lo (runtime init from kernel_lba) */
    0x00, 0x00, 0x00, 0x00, /* +12 LBA hi = 0 (drives < 2 TB) */

    /* 0x036: loop iteration counter — starts at 128 (128 × 8 sectors = 1024 = 512 KB).
     * Patched at install time by sw_stage_install to the exact kernel size.
     * The load loop uses DEC + JNZ, so it counts down N → 1 → 0 (exits at 0). */
    0x80,

    /* 0x037–0x054: banner (30 bytes) */
    'i','n','t','e','i','l','i','D','O','S',
    ' ','1','.','0',' ','-',' ',
    'L','o','a','d','i','n','g','.','.','.','\r','\n',0,

    /* 0x055: padding */
    0x00,

    /* ═══════════════════════════════════════════════════════════════════════
     * 0x056  Phase 1 — 16-bit real-mode bootstrap
     *
     * All INT 10h calls (including AH=0 mode-set and AH=0Eh teletype) are
     * avoided after the first one.  On the HP Vectra VEi8 (440BX/Award BIOS),
     * INT 10h AH=0 (Set Video Mode) hangs the BIOS handler and never returns.
     * INT 10h AH=0Eh works exactly once — subsequent calls crash or reset.
     *
     * Strategy:
     *   • One INT 10h AH=0Eh call prints '*' at cursor position.  This is
     *     the single reliable BIOS call.  It works in ANY video mode
     *     (text or graphical) because the BIOS draws to whatever framebuffer
     *     is active.  Seeing '*' on screen proves the VBR is executing.
     *   • No further BIOS calls.  A20, disk load, and PM switch are pure
     *     register and port I/O — no interrupts.
     *   • The kernel (running at 0x100000 in 32-bit PM) calls vga_set_mode3()
     *     which directly programs all VGA controller registers to text mode 3
     *     without using INT 10h.  Only the kernel needs a working display.
     * ═══════════════════════════════════════════════════════════════════════ */

    /* 0x056: Save boot drive DL before any register use */
    /* 0x056 */ 0x88,0x16,0x25,0x7C,            /* mov  [0x7C25], dl             */

    /* 0x05A–0x061: disk reset REMOVED (8 NOPs preserve all offsets).
     * INT 13h AH=00h was added to undo INT 10h state corruption, but INT 10h is
     * no longer called.  The reset itself returns E:00 (CF=1, AH=0 — Phoenix bug)
     * and appears to invalidate the BIOS drive-ready state before AH=42h.       */
    /* 0x05A */ 0x90,0x90,0x90,0x90,            /* nop nop nop nop               */
    /* 0x05E */ 0x90,0x90,                      /* nop nop                       */
    /* 0x060 */ 0x90,0x90,                      /* nop nop                       */

    /* 0x062: Segment and stack setup (DS=0, SS=0, SP=0x7C00) */
    /* 0x062 */ 0x31,0xC0,                      /* xor  ax, ax                   */
    /* 0x064 */ 0x8E,0xD8,                      /* mov  ds, ax                   */
    /* 0x066 */ 0x8E,0xD0,                      /* mov  ss, ax                   */
    /* 0x068 */ 0xBC,0x00,0x7C,                 /* mov  sp, 0x7C00               */
    /* 0x06B */ 0xFB,                           /* sti  (needed before INT 13h)  */

    /* 0x06C: Enable A20 via port 0x92 fast gate.
     * KBC-based A20 (polling port 0x64) removed: that bit never clears on
     * this machine, causing an infinite loop.  Port 0x92 is always safe.    */
    /* 0x06C */ 0xE4,0x92,                      /* in   al, 0x92                 */
    /* 0x06E */ 0x0C,0x02,                      /* or   al, 2    (A20 bit)       */
    /* 0x070 */ 0x24,0xFE,                      /* and  al, 0xFE (no reset bit)  */
    /* 0x072 */ 0xE6,0x92,                      /* out  0x92, al                 */

    /* 0x074: Copy kernel_lba (patched at install time) into the DAP LBA field */
    /* 0x074 */ 0x8B,0x1E,0x21,0x7C,            /* mov  bx, [0x7C21]  (lo word)  */
    /* 0x078 */ 0x89,0x1E,0x2E,0x7C,            /* mov  [0x7C2E], bx  (DAP LBA)  */
    /* 0x07C */ 0x8B,0x1E,0x23,0x7C,            /* mov  bx, [0x7C23]  (hi word)  */
    /* 0x080 */ 0x89,0x1E,0x30,0x7C,            /* mov  [0x7C30], bx             */
    /* 0x084 */ 0x31,0xDB,                      /* xor  bx, bx  (buf_seg = 0)    */

    /* 0x086 ←load_loop: read 64 × 8 sectors (256 KB total) to 0x8000–0x47FFF
     * Each INT 13h call reads 8 sectors (4 KB).  8 sectors is universally safe;
     * many Phoenix BIOSes on budget laptops cap extended reads at 16 or 32
     * sectors and silently fail with carry set if the count is higher.          */
    /* 0x086 */ 0x89,0x1E,0x2C,0x7C,            /* mov  [0x7C2C], bx  (buf seg)  */
    /* 0x08A */ 0xB2,0x80,0x90,0x90,            /* mov  dl, 0x80; nop; nop
                                                 * Hardcoded 0x80: this BIOS passes
                                                 * DL=0x00 to MBR (non-standard CSM
                                                 * behaviour) but INT 13h AH=42h in
                                                 * VBR context requires DL=0x80.    */
    /* 0x08E */ 0xB4,0x42,                      /* mov  ah, 0x42  (ext read)     */
    /* 0x090 */ 0xBE,0x26,0x7C,                 /* mov  si, 0x7C26  (→ DAP)      */
    /* 0x093 */ 0xCD,0x13,                      /* int  0x13                     */
    /* 0x095 */ 0x72,0x54,                      /* jc   +0x54 → 0x0EB (error)
                                                 * (+5 from INT 10h mode-set below) */
    /* 0x097 */ 0x81,0xC3,0x00,0x01,            /* add  bx, 0x0100  (next seg, 8×512/16=0x100) */
    /* 0x09B */ 0x66,0x83,0x06,0x2E,0x7C,0x08,  /* add  dword[0x7C2E], 8         */
    /* 0x0A1 */ 0xFE,0x0E,0x36,0x7C,            /* dec  byte[0x7C36]  (counter)  */
    /* 0x0A5 */ 0x75,0xDF,                      /* jnz  ←load_loop  (−33→0x086) */

    /* 0x0A7: Set VGA text mode 3 via BIOS — called AFTER all INT 13h disk reads,
     * so no disk-state corruption risk.  This is the only INT 10h call in the
     * normal boot path; the BIOS (HP Vectra VEi8 / Award BIOS) may leave the
     * display in a non-text mode, so 0xB8000 writes can be invisible
     * without this mode set.  Inserting here shifts Phase 2 and error-handler
     * addresses by +5; jc (0x095) and far-jmp target updated accordingly.     */
    /* 0x0A7 */ 0xB8,0x03,0x00,                 /* mov  ax, 0x0003  (mode 3)     */
    /* 0x0AA */ 0xCD,0x10,                       /* int  0x10                     */

    /* 0x0AC: Enter 32-bit protected mode (was 0x0A7; +5 due to INT 10h above) */
    /* 0x0AC */ 0x0F,0x01,0x16,0x1B,0x7C,       /* lgdt [0x7C1B]                 */
    /* 0x0AC */ 0xFA,                           /* cli                           */
    /* 0x0AD */ 0x0F,0x20,0xC0,                 /* mov  eax, cr0                 */
    /* 0x0B0 */ 0x0C,0x01,                      /* or   al, 1    (set PE)        */
    /* 0x0B2 */ 0x0F,0x22,0xC0,                 /* mov  cr0, eax                 */
    /* 0x0B5 */ 0xEA,0xBF,0x7C,0x08,0x00,       /* jmp  far 0x08:0x7CBF
                                                  * (was 0x7CBA; +5 for INT 10h)  */

    /* ═══════════════════════════════════════════════════════════════════════
     * 0x0BF  Phase 2 — 32-bit protected-mode: copy kernel to 0x100000, jump
     *        (was 0x0BA; +5 for INT 10h mode-set inserted at 0x0A7)
     * ═══════════════════════════════════════════════════════════════════════ */
    /* 0x0BF */ 0xB8,0x10,0x00,0x00,0x00,        /* mov  eax, 0x10  (data sel)    */
    /* 0x0BF */ 0x8E,0xD8,                      /* mov  ds, ax                   */
    /* 0x0C1 */ 0x8E,0xC0,                      /* mov  es, ax                   */
    /* 0x0C3 */ 0x8E,0xD0,                      /* mov  ss, ax                   */
    /* 0x0C5 */ 0xBC,0x00,0x00,0x09,0x00,        /* mov  esp, 0x90000             */
    /* 0x0CA */ 0xBE,0x00,0x80,0x00,0x00,        /* mov  esi, 0x8000              */
    /* 0x0CF */ 0xBF,0x00,0x00,0x10,0x00,        /* mov  edi, 0x100000            */
    /* 0x0D4 */ 0xB9,0x00,0x00,0x02,0x00,        /* mov  ecx, 0x20000  (131072 dw) — patched at install time */
    /* 0x0D9 */ 0xF3,0xA5,                      /* rep  movsd  (up to 512 KB)    */
    /* 0x0DB */ 0x31,0xC0,                      /* xor  eax, eax                 */
    /* 0x0DD */ 0x31,0xDB,                      /* xor  ebx, ebx                 */
    /* 0x0DF */ 0xEA,0x00,0x00,0x10,0x00,0x08,0x00, /* jmp  far 0x08:0x100000   */

    /* ═══════════════════════════════════════════════════════════════════════
     * 0x0E6  Error handler — reached by jc from INT 13h on disk read failure.
     *
     * Diagnostic output:
     *   1. Print "E:XX" via INT 10h AH=0Eh (BIOS teletype).  INT 10h is safe
     *      here because the disk read has already failed — there is no further
     *      INT 13h call for it to corrupt.  BIOS teletype works in ANY video
     *      mode the firmware uses (text, VESA, EFI GOP legacy) unlike a direct
     *      write to 0xB8000 which only works in VGA text mode 3.
     *   2. Ring the PC speaker (PIT ch2 + port 0x61) so the error is audible
     *      even if the screen is blank.
     *
     * At entry: AH = INT 13h error code (BIOS sets AH + CF=1 on failure).
     *
     * Common INT 13h error codes (Phoenix BIOS):
     *   0x01 = invalid parameter in DAP (bad size/count/buffer)
     *   0x04 = sector not found / LBA out of range
     *   0x20 = controller failure
     *   0x40 = seek failure
     *   0x80 = drive timeout / not ready
     *   0xAA = drive not ready (alternate)
     * ═══════════════════════════════════════════════════════════════════════ */

    /* 0x0E6: save error code in BH; clear BX (page 0) */
    /* 0x0E6 */ 0x8A,0xFC,                      /* mov  bh, ah   (save err code) */
    /* 0x0E8 */ 0x31,0xDB,                      /* xor  bx, bx   (page 0, col 0) */

    /* 0x0EA: print 'E' via INT 10h AH=0Eh (teletype) */
    /* 0x0EA */ 0xB0,0x45,                      /* mov  al, 'E'                  */
    /* 0x0EC */ 0xB4,0x0E,                      /* mov  ah, 0x0E                 */
    /* 0x0EE */ 0xCD,0x10,                      /* int  0x10                     */

    /* 0x0F0: print ':' */
    /* 0x0F0 */ 0xB0,0x3A,                      /* mov  al, ':'                  */
    /* 0x0F2 */ 0xB4,0x0E,                      /* mov  ah, 0x0E                 */
    /* 0x0F4 */ 0xCD,0x10,                      /* int  0x10                     */

    /* 0x0F6: print high nibble of BH as hex digit */
    /* 0x0F6 */ 0x8A,0xC7,                      /* mov  al, bh                   */
    /* 0x0F8 */ 0xC0,0xE8,0x04,                 /* shr  al, 4                    */
    /* 0x0FB */ 0x04,0x30,                      /* add  al, '0'                  */
    /* 0x0FD */ 0x3C,0x3A,                      /* cmp  al, ':'                  */
    /* 0x0FF */ 0x7C,0x02,                      /* jl   0x103  (digit 0-9)       */
    /* 0x101 */ 0x04,0x07,                      /* add  al, 7  (adjust A-F)      */
    /* 0x103 */ 0xB4,0x0E,                      /* mov  ah, 0x0E                 */
    /* 0x105 */ 0xCD,0x10,                      /* int  0x10                     */

    /* 0x107: print low nibble of BH as hex digit */
    /* 0x107 */ 0x8A,0xC7,                      /* mov  al, bh                   */
    /* 0x109 */ 0x24,0x0F,                      /* and  al, 0x0F                 */
    /* 0x10B */ 0x04,0x30,                      /* add  al, '0'                  */
    /* 0x10D */ 0x3C,0x3A,                      /* cmp  al, ':'                  */
    /* 0x10F */ 0x7C,0x02,                      /* jl   0x113                    */
    /* 0x111 */ 0x04,0x07,                      /* add  al, 7                    */
    /* 0x113 */ 0xB4,0x0E,                      /* mov  ah, 0x0E                 */
    /* 0x115 */ 0xCD,0x10,                      /* int  0x10                     */

    /* 0x117: print " D:YY" where YY = drive number saved at [0x7C25].
     * This shows the DL value that was passed to INT 13h AH=42h, so together
     * with the AH error code we can distinguish "wrong drive" from other errors. */
    /* 0x117 */ 0xB0,0x20,0xB4,0x0E,0xCD,0x10,  /* print ' '                     */
    /* 0x11D */ 0xB0,0x44,0xB4,0x0E,0xCD,0x10,  /* print 'D'                     */
    /* 0x123 */ 0xB0,0x3A,0xB4,0x0E,0xCD,0x10,  /* print ':'                     */
    /* 0x129 */ 0x8A,0x3E,0x25,0x7C,            /* mov  bh, [0x7C25]  (saved DL) */

    /* 0x12D: high nibble of DL */
    /* 0x12D */ 0x8A,0xC7,                      /* mov  al, bh                   */
    /* 0x12F */ 0xC0,0xE8,0x04,                 /* shr  al, 4                    */
    /* 0x132 */ 0x04,0x30,                      /* add  al, '0'                  */
    /* 0x134 */ 0x3C,0x3A,                      /* cmp  al, ':'                  */
    /* 0x136 */ 0x7C,0x02,                      /* jl   0x13A                    */
    /* 0x138 */ 0x04,0x07,                      /* add  al, 7                    */
    /* 0x13A */ 0xB4,0x0E,                      /* mov  ah, 0x0E                 */
    /* 0x13C */ 0xCD,0x10,                      /* int  0x10                     */

    /* 0x13E: low nibble of DL */
    /* 0x13E */ 0x8A,0xC7,                      /* mov  al, bh                   */
    /* 0x140 */ 0x24,0x0F,                      /* and  al, 0x0F                 */
    /* 0x142 */ 0x04,0x30,                      /* add  al, '0'                  */
    /* 0x144 */ 0x3C,0x3A,                      /* cmp  al, ':'                  */
    /* 0x146 */ 0x7C,0x02,                      /* jl   0x14A                    */
    /* 0x148 */ 0x04,0x07,                      /* add  al, 7                    */
    /* 0x14A */ 0xB4,0x0E,                      /* mov  ah, 0x0E                 */
    /* 0x14C */ 0xCD,0x10,                      /* int  0x10                     */

    /* 0x14E: PC speaker tone (PIT ch2, ~880 Hz) */
    /* 0x14E */ 0xB0,0xB6,                      /* mov  al, 0xB6  (PIT cmd)      */
    /* 0x150 */ 0xE6,0x43,                      /* out  0x43, al                 */
    /* 0x152 */ 0xB0,0x4B,                      /* mov  al, 0x4B  (div lo)       */
    /* 0x154 */ 0xE6,0x42,                      /* out  0x42, al                 */
    /* 0x156 */ 0xB0,0x05,                      /* mov  al, 0x05  (div hi)       */
    /* 0x158 */ 0xE6,0x42,                      /* out  0x42, al                 */
    /* 0x15A */ 0xE4,0x61,                      /* in   al, 0x61                 */
    /* 0x15C */ 0x0C,0x03,                      /* or   al, 0x03                 */
    /* 0x15E */ 0xE6,0x61,                      /* out  0x61, al  (speaker on)   */
    /* 0x160 */ 0xFA,                           /* cli                           */
    /* 0x161 */ 0xF4,                           /* hlt                           */
    /* 0x162 */ 0xEB,0xFD,                      /* jmp  −3 → 0x161 (loop)       */

    /* 0x169–0x1FD: zero fill (149 bytes)
     * 5 fewer zeros than original (154) compensate for the 5-byte INT 10h
     * mode-set inserted at 0x0A7; keeps 0x55 0xAA at exactly 0x1FE–0x1FF. */
    0,0,0,0,0,0,0,0,0,0, 0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0, 0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0, 0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0, 0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0, 0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0, 0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0, 0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,

    /* 0x1FE–0x1FF: boot signature */
    0x55, 0xAA
};

/* =========================================================================
 * Partition table structures (packed, MBR-compatible)
 * ========================================================================= */
typedef struct {
    uint8_t  status;        /* 0x80 = bootable, 0x00 = inactive */
    uint8_t  chs_first[3]; /* CHS of first sector (or 0xFE/FF/FF for >8GB) */
    uint8_t  type;          /* partition type byte */
    uint8_t  chs_last[3];  /* CHS of last sector  */
    uint32_t lba_start;    /* LBA of first sector */
    uint32_t lba_size;     /* size in 512-byte sectors */
} __attribute__((packed)) sw_part_t;

#define SW_PART_TYPE_INTEILIDOS  0x99   /* inteiliDOS custom type */
#define SW_PART_TABLE_OFF        0x1BE  /* offset in MBR/sector */
#define SW_BOOT_SIG_OFF          0x1FE

/* First partition sector (1 MB boundary, standard) */
#define SW_PART_LBA_START  2048u

/* Sectors reserved for VBR + kernel after partition start */
#define SW_KERNEL_SECTORS  1024u        /* 512 KB  — covers any build */
#define SW_KERNEL_RAM_ADDR 0x00100000u  /* where multiboot loads us */

/* =========================================================================
 * Global wizard state
 * ========================================================================= */
#define SW_MODE_DUALBOOT   0
#define SW_MODE_CLEAN      1

static int          sw_mode;          /* installation mode */
static int          sw_drive;         /* chosen drive index (0-3) */
static ata_drive_t  sw_drives[ATA_MAX_DRIVES];
static int          sw_ndrives;

/* Sector-sized scratch buffer — reused throughout */
static uint8_t      sw_sector[512];

/* =========================================================================
 * Formatting helpers
 * ========================================================================= */
static void sw_fmt_size(uint32_t sectors, char *out, int max) {
    /* sectors * 512 bytes → MB */
    uint32_t mb = sectors / 2048u;
    if (mb >= 1024) {
        uint32_t gb = mb / 1024;
        char gs[8]; sw_uint_to_str(gb, gs, 8);
        int i = 0;
        for (; gs[i] && i < max-4; i++) out[i] = gs[i];
        out[i++] = ' '; out[i++] = 'G'; out[i++] = 'B'; out[i] = '\0';
    } else {
        char ms[8]; sw_uint_to_str(mb, ms, 8);
        int i = 0;
        for (; ms[i] && i < max-4; i++) out[i] = ms[i];
        out[i++] = ' '; out[i++] = 'M'; out[i++] = 'B'; out[i] = '\0';
    }
}

/* Build a one-line drive description: "0: ATA  ModelName  [120 GB]" */
static void sw_drive_line(int idx, char *out, int max) {
    ata_drive_t *d = &sw_drives[idx];
    char sz[12]; sw_fmt_size(d->total_sectors, sz, 12);
    int i = 0;
    out[i++] = (char)('0' + idx);
    out[i++] = ':'; out[i++] = ' ';
    /* truncate model to 28 chars */
    for (int j = 0; d->model[j] && j < 28 && i < max-20; j++)
        out[i++] = d->model[j];
    /* pad + size */
    out[i++] = ' '; out[i++] = ' '; out[i++] = '[';
    for (int j = 0; sz[j] && i < max-2; j++) out[i++] = sz[j];
    out[i++] = ']';
    out[i] = '\0';
}

/* =========================================================================
 * Progress bar (drawn inside the content area)
 * ========================================================================= */
static void sw_progress_bar(int content_row, int pct, const char *label) {
    /* Draw label */
    sw_content_line(content_row, label, VGA_COLOR_WHITE, VGA_COLOR_BLUE);

    /* Draw bar row below */
    int sr = 3 + content_row + 1;
    if (sr > 20) return;

    int bar_w  = 60;
    int bar_col = 10;
    int filled  = bar_w * pct / 100;

    sw_poke(sr, 0, SW_V, VGA_COLOR_WHITE, VGA_COLOR_BLUE);
    sw_fill(sr, 1, ' ', VGA_COLOR_WHITE, VGA_COLOR_BLUE, bar_col - 1);

    sw_poke(sr, bar_col, '[', VGA_COLOR_WHITE, VGA_COLOR_BLUE);
    for (int i = 0; i < bar_w; i++) {
        vga_color_t fg = (i < filled) ? VGA_COLOR_BLACK       : VGA_COLOR_DARK_GREY;
        vga_color_t bg = (i < filled) ? VGA_COLOR_LIGHT_GREEN : VGA_COLOR_BLUE;
        sw_poke(sr, bar_col + 1 + i, (unsigned char)(i < filled ? '\xDB' : '-'), fg, bg);
    }
    sw_poke(sr, bar_col + bar_w + 1, ']', VGA_COLOR_WHITE, VGA_COLOR_BLUE);

    /* Percentage */
    char ps[5];
    sw_uint_to_str((uint32_t)pct, ps, 5);
    int plen = sw_strlen(ps);
    int pcol = bar_col + bar_w + 3;
    sw_puts(sr, pcol, ps, VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLUE, plen);
    sw_poke(sr, pcol + plen, '%', VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLUE);

    sw_fill(sr, pcol + plen + 1, ' ', VGA_COLOR_WHITE, VGA_COLOR_BLUE,
            78 - (pcol + plen + 1));
    sw_poke(sr, 79, SW_V, VGA_COLOR_WHITE, VGA_COLOR_BLUE);
}

/* =========================================================================
 * Stage rendering functions
 * ========================================================================= */

/* ── Stage 1: Welcome ───────────────────────────────────────────────────── */
static void sw_stage_welcome(void) {
    sw_draw_frame();
    sw_draw_header(1, 5);
    sw_clear_content();

    sw_content_line( 1, "", VGA_COLOR_WHITE, VGA_COLOR_BLUE);
    sw_content_line( 2, "  Welcome to the inteiliDOS Setup Wizard",
                     VGA_COLOR_LIGHT_CYAN, VGA_COLOR_BLUE);
    sw_content_line( 3, "  HP Vectra VEi8 Edition  (Intel 440BX / PIIX4E / Award BIOS)",
                     VGA_COLOR_DARK_GREY, VGA_COLOR_BLUE);
    sw_content_line( 4, "", VGA_COLOR_WHITE, VGA_COLOR_BLUE);
    sw_content_line( 5, "  This wizard installs inteiliDOS directly onto the HP Vectra's",
                     VGA_COLOR_WHITE, VGA_COLOR_BLUE);
    sw_content_line( 6, "  IDE hard drive and configures it to boot automatically.",
                     VGA_COLOR_WHITE, VGA_COLOR_BLUE);
    sw_content_line( 7, "  The kernel is copied sector-by-sector from RAM to the HDD.",
                     VGA_COLOR_WHITE, VGA_COLOR_BLUE);
    sw_content_line( 8, "", VGA_COLOR_WHITE, VGA_COLOR_BLUE);
    sw_content_line( 9, "  You will be asked to choose:", VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLUE);
    sw_content_line(10, "    \x07  Installation mode  (dual-boot or clean install)",
                     VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLUE);
    sw_content_line(11, "    \x07  Target IDE drive  (primary or secondary channel)",
                     VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLUE);
    sw_content_line(12, "", VGA_COLOR_WHITE, VGA_COLOR_BLUE);
    sw_content_line(13,
        "  WARNING: the clean-install option will permanently erase all",
        VGA_COLOR_LIGHT_RED, VGA_COLOR_BLUE);
    sw_content_line(14,
        "  data on the selected drive.  Back up your data before proceeding.",
        VGA_COLOR_LIGHT_RED, VGA_COLOR_BLUE);
    sw_content_line(15, "", VGA_COLOR_WHITE, VGA_COLOR_BLUE);
    sw_content_line(16,
        "  Press ENTER to continue or ESC to cancel.",
        VGA_COLOR_DARK_GREY, VGA_COLOR_BLUE);

    sw_draw_footer("Enter=Continue", "Esc=Cancel");
    vga_set_cursor(25, 0);
}

/* ── Stage 2: Mode selection ─────────────────────────────────────────────── */
static int sw_stage_mode(void) {
    int sel = 0;
    while (1) {
        sw_draw_frame();
        sw_draw_header(2, 5);
        sw_clear_content();

        sw_content_line(1, "  Choose an installation mode:", VGA_COLOR_LIGHT_CYAN, VGA_COLOR_BLUE);
        sw_content_line(2, "", VGA_COLOR_WHITE, VGA_COLOR_BLUE);

        sw_menu_item(3, "Install alongside existing OS  (dual-boot)", sel == 0);
        sw_content_line(4,
            "    Adds inteiliDOS as a new partition.  Existing data is preserved.",
            VGA_COLOR_DARK_GREY, VGA_COLOR_BLUE);
        sw_content_line(5, "", VGA_COLOR_WHITE, VGA_COLOR_BLUE);

        sw_menu_item(6, "Erase drive and install inteiliDOS  (clean install)", sel == 1);
        sw_content_line(7,
            "    Removes all existing partitions and data.  Cannot be undone.",
            VGA_COLOR_LIGHT_RED, VGA_COLOR_BLUE);

        sw_draw_footer("\x18\x19=Select   Enter=Continue", "Esc=Back");
        vga_set_cursor(25, 0);

        int k = keyboard_getchar();
        if (k == KEY_UP   && sel > 0) sel--;
        if (k == KEY_DOWN && sel < 1) sel++;
        if (k == KEY_ENTER)  { sw_mode = sel; return 1; }
        if (k == KEY_ESCAPE) return 0;
    }
}

/* ── Stage 3: Drive selection ────────────────────────────────────────────── */
static int sw_stage_drive(void) {
    /* Run live ATA detection */
    sw_draw_frame();
    sw_draw_header(3, 5);
    sw_clear_content();
    sw_content_line(2, "  Detecting drives...", VGA_COLOR_LIGHT_CYAN, VGA_COLOR_BLUE);
    vga_set_cursor(25, 0);

    sw_ndrives = ata_detect(sw_drives);

    int sel = 0;
    while (1) {
        sw_draw_frame();
        sw_draw_header(3, 5);
        sw_clear_content();

        sw_content_line(1, "  Select the target installation drive:", VGA_COLOR_LIGHT_CYAN, VGA_COLOR_BLUE);
        sw_content_line(2, "", VGA_COLOR_WHITE, VGA_COLOR_BLUE);

        if (sw_ndrives == 0) {
            sw_content_line(3, "  No ATA drives detected.", VGA_COLOR_LIGHT_RED, VGA_COLOR_BLUE);
            sw_content_line(5, "  Press ESC to cancel.", VGA_COLOR_DARK_GREY, VGA_COLOR_BLUE);
            sw_draw_footer("", "Esc=Cancel");
            vga_set_cursor(25, 0);
            int k = keyboard_getchar();
            if (k == KEY_ESCAPE || k == KEY_ENTER) return 0;
            continue;
        }

        int row = 3;
        int vi  = 0;    /* visible index among present drives */
        for (int i = 0; i < ATA_MAX_DRIVES; i++) {
            if (!sw_drives[i].present) continue;
            char line[72];
            sw_drive_line(i, line, 72);
            sw_menu_item(row++, line, vi == sel);
            vi++;
        }

        sw_content_line(row + 1,
            "  NOTE: On dual-boot installs the drive must have a free",
            VGA_COLOR_DARK_GREY, VGA_COLOR_BLUE);
        sw_content_line(row + 2,
            "  partition slot.  On clean installs all data will be erased.",
            VGA_COLOR_DARK_GREY, VGA_COLOR_BLUE);

        sw_draw_footer("\x18\x19=Select   Enter=Continue", "Esc=Back");
        vga_set_cursor(25, 0);

        int k = keyboard_getchar();
        if (k == KEY_UP   && sel > 0) sel--;
        if (k == KEY_DOWN && sel < sw_ndrives - 1) sel++;
        if (k == KEY_ESCAPE) return 0;
        if (k == KEY_ENTER) {
            /* Map visible-index sel back to the drive index */
            int vi2 = 0;
            for (int i = 0; i < ATA_MAX_DRIVES; i++) {
                if (!sw_drives[i].present) continue;
                if (vi2 == sel) { sw_drive = i; return 1; }
                vi2++;
            }
        }
    }
}

/* ── Stage 4: Confirmation ───────────────────────────────────────────────── */
static int sw_stage_confirm(void) {
    sw_draw_frame();
    sw_draw_header(4, 5);
    sw_clear_content();

    char dline[72]; sw_drive_line(sw_drive, dline, 72);
    char szline[48];
    {
        char sz[16]; sw_fmt_size(sw_drives[sw_drive].total_sectors, sz, 16);
        const char *pfx = "  Drive size: ";
        int i = 0;
        for (; pfx[i]; i++) szline[i] = pfx[i];
        for (int j = 0; sz[j] && i < 47; j++) szline[i++] = sz[j];
        szline[i] = '\0';
    }

    sw_content_line( 1, "  Please review your selections:", VGA_COLOR_LIGHT_CYAN, VGA_COLOR_BLUE);
    sw_content_line( 2, "", VGA_COLOR_WHITE, VGA_COLOR_BLUE);
    sw_content_line( 3, "  Target drive:", VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLUE);
    {
        char dl2[72];
        dl2[0] = ' '; dl2[1] = ' '; dl2[2] = ' '; dl2[3] = ' ';
        sw_strcpy(dl2 + 4, dline, 68);
        sw_content_line(4, dl2, VGA_COLOR_WHITE, VGA_COLOR_BLUE);
    }
    sw_content_line( 5, szline, VGA_COLOR_WHITE, VGA_COLOR_BLUE);
    sw_content_line( 6, "", VGA_COLOR_WHITE, VGA_COLOR_BLUE);
    sw_content_line( 7, "  Installation mode:", VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLUE);
    if (sw_mode == SW_MODE_DUALBOOT) {
        sw_content_line(8, "    Dual-boot  —  existing data preserved, new partition added",
                        VGA_COLOR_WHITE, VGA_COLOR_BLUE);
    } else {
        sw_content_line(8, "    Clean install  —  ALL DATA ON THIS DRIVE WILL BE ERASED",
                        VGA_COLOR_LIGHT_RED, VGA_COLOR_BLUE);
    }
    sw_content_line( 9, "", VGA_COLOR_WHITE, VGA_COLOR_BLUE);
    if (sw_mode == SW_MODE_CLEAN) {
        sw_content_line(10,
            "  \x07 This operation is irreversible.  All partitions and files on",
            VGA_COLOR_LIGHT_RED, VGA_COLOR_BLUE);
        sw_content_line(11,
            "    the selected drive will be permanently deleted.",
            VGA_COLOR_LIGHT_RED, VGA_COLOR_BLUE);
        sw_content_line(12, "", VGA_COLOR_WHITE, VGA_COLOR_BLUE);
        sw_content_line(13, "  Press ENTER to begin installation or ESC to go back.",
                        VGA_COLOR_DARK_GREY, VGA_COLOR_BLUE);
    } else {
        sw_content_line(10, "  Press ENTER to begin installation or ESC to go back.",
                        VGA_COLOR_DARK_GREY, VGA_COLOR_BLUE);
    }

    sw_draw_footer("Enter=Install", "Esc=Back");
    vga_set_cursor(25, 0);

    while (1) {
        int k = keyboard_getchar();
        if (k == KEY_ENTER)  return 1;
        if (k == KEY_ESCAPE) return 0;
    }
}

/* =========================================================================
 * Stage 5: Installation — real disk writes
 * ========================================================================= */

/* Build a 512-byte MBR sector in sw_sector[].
 * dual_boot=1 → preserve existing partition entries, splice in a new one.
 * dual_boot=0 → zero all entries, create a single inteiliDOS partition.
 */
static int sw_build_mbr(int dual_boot) {
    uint32_t total   = sw_drives[sw_drive].total_sectors;
    uint8_t  drv     = (uint8_t)sw_drive;

    if (dual_boot) {
        /* Read existing MBR */
        if (ata_read_sector(drv, 0, sw_sector) != 0) return -1;
    } else {
        /* Zero everything */
        kmemset(sw_sector, 0, 512);
    }

    /* Copy our bootstrap into the first 446 bytes */
    for (int i = 0; i < 446; i++) sw_sector[i] = mbr_bootstrap[i];

    /* Point to the four partition entries */
    sw_part_t *pt = (sw_part_t *)(sw_sector + SW_PART_TABLE_OFF);

    if (!dual_boot) {
        /* Single inteiliDOS partition covering the whole drive */
        kmemset(pt, 0, 4 * sizeof(sw_part_t));
        pt[0].status    = 0x80;                  /* bootable */
        pt[0].type      = SW_PART_TYPE_INTEILIDOS;
        pt[0].chs_first[0] = 0xFE;               /* CHS overflow → use LBA */
        pt[0].chs_first[1] = 0xFF;
        pt[0].chs_first[2] = 0xFF;
        pt[0].chs_last[0]  = 0xFE;
        pt[0].chs_last[1]  = 0xFF;
        pt[0].chs_last[2]  = 0xFF;
        pt[0].lba_start = SW_PART_LBA_START;
        pt[0].lba_size  = (total > SW_PART_LBA_START)
                          ? (total - SW_PART_LBA_START) : 1;
    } else {
        /* Dual-boot: find an empty slot */
        int slot = -1;
        for (int i = 0; i < 4; i++) {
            if (pt[i].type == 0 && pt[i].lba_start == 0) {
                slot = i; break;
            }
        }
        if (slot < 0) return -2;   /* no free slot */

        /* Find the end of the last existing partition */
        uint32_t part_start = SW_PART_LBA_START;
        for (int i = 0; i < 4; i++) {
            if (pt[i].type != 0) {
                uint32_t end = pt[i].lba_start + pt[i].lba_size;
                if (end > part_start) part_start = end;
            }
        }

        /* Clear active flag on all existing entries */
        for (int i = 0; i < 4; i++) pt[i].status = 0x00;

        pt[slot].status    = 0x80;
        pt[slot].type      = SW_PART_TYPE_INTEILIDOS;
        pt[slot].chs_first[0] = 0xFE;
        pt[slot].chs_first[1] = 0xFF;
        pt[slot].chs_first[2] = 0xFF;
        pt[slot].chs_last[0]  = 0xFE;
        pt[slot].chs_last[1]  = 0xFF;
        pt[slot].chs_last[2]  = 0xFF;
        pt[slot].lba_start = part_start;
        pt[slot].lba_size  = (total > part_start + 2048)
                             ? (total - part_start) : 2048;
    }

    /* Boot signature */
    sw_sector[SW_BOOT_SIG_OFF]     = 0x55;
    sw_sector[SW_BOOT_SIG_OFF + 1] = 0xAA;

    return 0;
}

/* Get the LBA start of our inteiliDOS partition from sw_sector (which
 * should hold the MBR we just built). */
static uint32_t sw_get_part_lba(void) {
    sw_part_t *pt = (sw_part_t *)(sw_sector + SW_PART_TABLE_OFF);
    for (int i = 0; i < 4; i++) {
        if (pt[i].type == SW_PART_TYPE_INTEILIDOS && pt[i].status == 0x80)
            return pt[i].lba_start;
    }
    return SW_PART_LBA_START;   /* fallback */
}

/* Draw the installation progress screen and execute each step */
static int sw_stage_install(void) {
    sw_draw_frame();
    sw_draw_header(5, 5);
    sw_clear_content();
    sw_draw_footer("", "Do not power off");
    vga_set_cursor(25, 0);

    sw_content_line(0, "", VGA_COLOR_WHITE, VGA_COLOR_BLUE);
    sw_content_line(1, "  Installing inteiliDOS...", VGA_COLOR_LIGHT_CYAN, VGA_COLOR_BLUE);
    sw_content_line(2, "", VGA_COLOR_WHITE, VGA_COLOR_BLUE);

#define STEP(pct, msg, action) do { \
    sw_progress_bar(3, (pct), (msg)); \
    timer_sleep(350); \
    { action } \
} while(0)

    int rc;
    uint32_t part_lba;
    uint8_t  sw_vbr_iterations;   /* loop count patched into VBR at install time */

    /* Pre-compute how many sectors need loading so the VBR can be patched
     * in Step 3 before it is written to disk.  Each INT 13h call in the VBR
     * reads 8 sectors, so we round the sector count up to the next multiple
     * of 8 to keep the loop arithmetic exact.
     *
     * Root cause of the HDD-boot "no drives" bug: the kernel binary grew
     * past 256 KB (512 sectors).  The old hard-coded VBR limit of 512 sectors
     * cut off the copy before the .data section was reached, leaving
     * g_ata_base[] and g_ata_ctrl[] as zero — so all IDE I/O went to port 0.
     * We now compute the true sector count and patch it into the VBR so any
     * future growth is handled automatically.                               */
    {
        uint32_t ke  = (uint32_t)(uintptr_t)&_kernel_data_end;
        uint32_t ksz = (ke > SW_KERNEL_RAM_ADDR) ? (ke - SW_KERNEL_RAM_ADDR) : 512u;
        uint32_t ws  = (ksz + 511u) / 512u;
        if (ws > SW_KERNEL_SECTORS) ws = SW_KERNEL_SECTORS;
        ws = (ws + 7u) & ~7u;                  /* round up to multiple of 8  */
        if (ws > SW_KERNEL_SECTORS) ws = SW_KERNEL_SECTORS;
        sw_vbr_iterations = (uint8_t)(ws / 8u);
        if (sw_vbr_iterations == 0) sw_vbr_iterations = 1;
    }

    /* Step 1 — Prepare MBR */
    STEP(10, "  Preparing partition table...", {
        rc = sw_build_mbr(sw_mode == SW_MODE_DUALBOOT);
        if (rc == -2) {
            sw_content_line(7,
                "  ERROR: No free partition slot on this drive.",
                VGA_COLOR_LIGHT_RED, VGA_COLOR_BLUE);
            sw_content_line(8,
                "  Dual-boot requires at least one empty primary partition slot.",
                VGA_COLOR_LIGHT_RED, VGA_COLOR_BLUE);
            sw_content_line(9,
                "  Press any key to cancel.", VGA_COLOR_DARK_GREY, VGA_COLOR_BLUE);
            sw_draw_footer("", "Installation failed");
            vga_set_cursor(25, 0);
            keyboard_getchar();
            return 0;
        }
        if (rc != 0) {
            sw_content_line(7, "  ERROR: Could not read existing MBR.",
                            VGA_COLOR_LIGHT_RED, VGA_COLOR_BLUE);
            sw_content_line(8, "  Press any key to cancel.",
                            VGA_COLOR_DARK_GREY, VGA_COLOR_BLUE);
            sw_draw_footer("", "Installation failed");
            vga_set_cursor(25, 0);
            keyboard_getchar();
            return 0;
        }
        part_lba = sw_get_part_lba();
    });

    /* Step 2 — Write MBR */
    STEP(25, "  Writing Master Boot Record...", {
        if (ata_write_sector((uint8_t)sw_drive, 0, sw_sector) != 0) {
            sw_content_line(7, "  ERROR: MBR write failed. Check drive connections.",
                            VGA_COLOR_LIGHT_RED, VGA_COLOR_BLUE);
            sw_content_line(8, "  Press any key to cancel.",
                            VGA_COLOR_DARK_GREY, VGA_COLOR_BLUE);
            sw_draw_footer("", "Installation failed");
            vga_set_cursor(25, 0);
            keyboard_getchar();
            return 0;
        }
    });

    /* Step 3 — Write VBR */
    STEP(40, "  Writing Volume Boot Record...", {
        for (int i = 0; i < 512; i++) sw_sector[i] = vbr_code[i];
        /* Patch kernel LBA at VBR offset 0x021 (4-byte little-endian).
         * The kernel is written starting at part_lba+1; the VBR loader
         * reads this dword at runtime to know where to begin the ATA read. */
        uint32_t klba = part_lba + 1;
        sw_sector[0x21] = (uint8_t)( klba        & 0xFF);
        sw_sector[0x22] = (uint8_t)((klba >>  8) & 0xFF);
        sw_sector[0x23] = (uint8_t)((klba >> 16) & 0xFF);
        sw_sector[0x24] = (uint8_t)((klba >> 24) & 0xFF);

        /* Patch the loop iteration counter at VBR offset 0x036.
         * Each INT 13h call loads 8 sectors; sw_vbr_iterations calls load
         * the entire kernel including the .data section no matter how large
         * the binary has grown.  Without this patch the VBR defaults to
         * 128 iterations (512 KB) which may still truncate a big kernel. */
        sw_sector[0x36] = sw_vbr_iterations;

        /* Patch the Phase 2 'mov ecx' immediate at VBR offset 0x0DA.
         * ECX = iterations × 8 sectors × 128 dwords/sector.
         * This controls how many dwords Phase 2 copies from 0x8000→0x100000;
         * it must match the number of sectors loaded in Phase 1. */
        uint32_t vbr_ecx = (uint32_t)sw_vbr_iterations * 1024u;
        sw_sector[0xDA] = (uint8_t)( vbr_ecx        & 0xFF);
        sw_sector[0xDB] = (uint8_t)((vbr_ecx >>  8) & 0xFF);
        sw_sector[0xDC] = (uint8_t)((vbr_ecx >> 16) & 0xFF);
        sw_sector[0xDD] = (uint8_t)((vbr_ecx >> 24) & 0xFF);
        if (ata_write_sector((uint8_t)sw_drive, part_lba, sw_sector) != 0) {
            sw_content_line(7, "  ERROR: VBR write failed. Check drive connections.",
                            VGA_COLOR_LIGHT_RED, VGA_COLOR_BLUE);
            sw_content_line(8, "  Press any key to cancel.",
                            VGA_COLOR_DARK_GREY, VGA_COLOR_BLUE);
            sw_draw_footer("", "Installation failed");
            vga_set_cursor(25, 0);
            keyboard_getchar();
            return 0;
        }
    });

    /* Step 4 — Copy kernel from RAM to disk
     *
     * Compute the exact number of 512-byte sectors to write from the linker
     * symbol _kernel_data_end (first byte past .text/.rodata/.data, aligned
     * to a 512-byte boundary).  BSS above that address is zero-initialised
     * by the kernel's own startup code and need not be stored on disk.
     *
     * The VBR reads exactly sw_vbr_iterations × 8 sectors from disk (patched
     * into the VBR in Step 3).  Sectors beyond what we write here contain
     * whatever was previously on the disk; any garbage that lands in the BSS
     * region is harmless because C global arrays are re-initialised by the
     * kernel's own init code before use.                                   */
    STEP(55, "  Copying system kernel to drive...", {
        /* Exact kernel data size from linker symbol */
        uint32_t kend  = (uint32_t)(uintptr_t)&_kernel_data_end;
        uint32_t ksize = (kend > SW_KERNEL_RAM_ADDR)
                         ? (kend - SW_KERNEL_RAM_ADDR) : 512u;
        uint32_t write_sects = (ksize + 511u) / 512u;
        if (write_sects > SW_KERNEL_SECTORS) write_sects = SW_KERNEL_SECTORS;

        {
            /* Display kernel size on the progress screen */
            char kinfo[72];
            const char *pfx = "  Kernel size: ";
            int ki = 0;
            for (; pfx[ki]; ki++) kinfo[ki] = pfx[ki];
            char ks[12]; sw_uint_to_str(write_sects * 512u / 1024u, ks, 12);
            for (int j = 0; ks[j] && ki < 68; j++) kinfo[ki++] = ks[j];
            const char *sfx = " KB  ("; int fi = 0;
            for (; sfx[fi] && ki < 68; fi++) kinfo[ki++] = sfx[fi];
            char ss[8]; sw_uint_to_str(write_sects, ss, 8);
            for (int j = 0; ss[j] && ki < 68; j++) kinfo[ki++] = ss[j];
            const char *sfx2 = " sectors)"; fi = 0;
            for (; sfx2[fi] && ki < 70; fi++) kinfo[ki++] = sfx2[fi];
            kinfo[ki] = '\0';
            sw_content_line(5, kinfo, VGA_COLOR_DARK_GREY, VGA_COLOR_BLUE);
        }

        const uint8_t *kptr = (const uint8_t *)SW_KERNEL_RAM_ADDR;
        for (uint32_t s = 0; s < write_sects; s++) {
            if (ata_write_sector((uint8_t)sw_drive,
                                 part_lba + 1 + s,
                                 kptr + s * 512) != 0) {
                sw_content_line(7, "  ERROR: Kernel write failed — disk may be full or faulty.",
                                VGA_COLOR_LIGHT_RED, VGA_COLOR_BLUE);
                {
                    char em[64];
                    const char *pfx = "  Failed at sector ";
                    int ei = 0;
                    for (; pfx[ei]; ei++) em[ei] = pfx[ei];
                    char ns[12]; sw_uint_to_str(s, ns, 12);
                    for (int j = 0; ns[j] && ei < 62; j++) em[ei++] = ns[j];
                    em[ei] = '\0';
                    sw_content_line(8, em, VGA_COLOR_LIGHT_RED, VGA_COLOR_BLUE);
                }
                sw_content_line(9, "  Press any key to cancel.",
                                VGA_COLOR_DARK_GREY, VGA_COLOR_BLUE);
                sw_draw_footer("", "Installation failed");
                vga_set_cursor(25, 0);
                keyboard_getchar();
                return 0;
            }
            /* Update progress bar every 32 sectors */
            if ((s & 31) == 0) {
                int pct = 55 + (int)(s * 25 / write_sects);
                sw_progress_bar(3, pct, "  Copying system kernel to drive...");
            }
        }
    });

    /* Step 5 — Verify boot signature */
    STEP(85, "  Verifying installation...", {
        kmemset(sw_sector, 0, 512);
        int vok = 0;
        if (ata_read_sector((uint8_t)sw_drive, 0, sw_sector) == 0) {
            vok = (sw_sector[0x1FE] == 0x55 && sw_sector[0x1FF] == 0xAA);
        }
        if (!vok) {
            sw_content_line(7, "  WARNING: Boot signature verification failed.",
                            VGA_COLOR_BROWN, VGA_COLOR_BLUE);
        }
    });

    /* Step 6 — Done */
    STEP(100, "  Installation complete!         ", { timer_sleep(300); });

#undef STEP

    /* Success screen within the same content area */
    sw_clear_content();
    sw_content_line(1, "", VGA_COLOR_WHITE, VGA_COLOR_BLUE);
    sw_content_line(2, "  \x02  Installation successful!",
                    VGA_COLOR_LIGHT_GREEN, VGA_COLOR_BLUE);
    sw_content_line(3, "", VGA_COLOR_WHITE, VGA_COLOR_BLUE);
    sw_content_line(4, "  inteiliDOS has been installed and configured to boot",
                    VGA_COLOR_WHITE, VGA_COLOR_BLUE);
    sw_content_line(5, "  automatically from the selected drive.",
                    VGA_COLOR_WHITE, VGA_COLOR_BLUE);
    sw_content_line(6, "", VGA_COLOR_WHITE, VGA_COLOR_BLUE);

    {
        char ll[72];
        ll[0]=' '; ll[1]=' '; ll[2]='D'; ll[3]='r'; ll[4]='i'; ll[5]='v';
        ll[6]='e'; ll[7]=':'; ll[8]=' ';
        char dl[60]; sw_drive_line(sw_drive, dl, 60);
        sw_strcpy(ll + 9, dl, 62);
        sw_content_line(7, ll, VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLUE);
    }

    sw_content_line( 8, "", VGA_COLOR_WHITE, VGA_COLOR_BLUE);
    sw_content_line( 9, "  Next steps (HP Vectra VEi8):", VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLUE);
    sw_content_line(10, "    1. Power off and remove the boot CD from the drive.",
                    VGA_COLOR_WHITE, VGA_COLOR_BLUE);
    sw_content_line(11, "    2. Power on.  Press F2 at the HP logo to enter Award BIOS setup.",
                    VGA_COLOR_WHITE, VGA_COLOR_BLUE);
    sw_content_line(12, "    3. Go to  Boot  \xAF  Boot Device Priority.",
                    VGA_COLOR_WHITE, VGA_COLOR_BLUE);
    sw_content_line(13, "       Move the IDE HDD to position 1.  Press F10 to save and exit.",
                    VGA_COLOR_WHITE, VGA_COLOR_BLUE);
    sw_content_line(14, "    4. Reboot — inteiliDOS will start automatically from the HDD.",
                    VGA_COLOR_WHITE, VGA_COLOR_BLUE);
    sw_content_line(15, "", VGA_COLOR_WHITE, VGA_COLOR_BLUE);
    sw_content_line(16, "  Run DISKCHECK at the shell to verify the installation on disk.",
                    VGA_COLOR_DARK_GREY, VGA_COLOR_BLUE);
    sw_content_line(17, "", VGA_COLOR_WHITE, VGA_COLOR_BLUE);
    sw_content_line(18, "  Press R to reboot now or any other key to return to shell.",
                    VGA_COLOR_DARK_GREY, VGA_COLOR_BLUE);

    sw_draw_footer("R=Reboot now   Any key=Return to shell", "");
    vga_set_cursor(25, 0);
    return 1;
}

/* =========================================================================
 * Main entry point
 * ========================================================================= */
void setup_run(void) {
    /* Blank screen */
    for (int r = 0; r < 25; r++)
        for (int c = 0; c < 80; c++)
            sw_poke(r, c, ' ', VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLUE);

    /* Stage 1: welcome */
    sw_stage_welcome();
    while (1) {
        int k = keyboard_getchar();
        if (k == KEY_ESCAPE) goto done;
        if (k == KEY_ENTER)  break;
    }

    /* Stage 2: mode */
    if (!sw_stage_mode()) goto done;

    /* Stage 3: drive */
    if (!sw_stage_drive()) goto done;

    /* Stage 4: confirmation */
    if (!sw_stage_confirm()) goto done;

    /* Stage 5: install */
    {
        int result = sw_stage_install();
        if (result) {
            /* Wait for R or any key */
            int k = keyboard_getchar();
            if (k == 'r' || k == 'R') {
                /* Reboot via keyboard controller */
                __asm__ volatile (
                    "cli\n\t"
                    "1: inb $0x64, %%al\n\t"
                    "testb $0x02, %%al\n\t"
                    "jnz 1b\n\t"
                    "movb $0xFE, %%al\n\t"
                    "outb %%al, $0x64\n\t"
                    "hlt"
                    ::: "eax", "memory"
                );
            }
        }
    }

done:
    vga_clear();
    vga_set_color(VGA_COLOR_WHITE, VGA_COLOR_BLACK);
    vga_set_cursor(0, 0);
}
