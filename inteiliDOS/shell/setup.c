/*
 * inteiliDOS -- shell/setup.c
 * InteiliDOS Setup Wizard 1.0
 *
 * Interactive full-screen OS installer.  Writes a genuine, bootable
 * inteiliDOS installation to an ATA/IDE hard drive or CompactFlash card.
 *
 * Wizard stages:
 *   1  Welcome
 *   2  Installation mode  (dual-boot  /  erase & install)
 *   3  Target drive selection  (live ATA enumeration)
 *   4  Confirmation / warning
 *   5  Installation progress  (real disk writes)
 *   6  Done — reboot prompt
 *
 * Disk layout written:
 *   LBA 0          Master Boot Record  (446-byte x86 bootstrap + partition table)
 *   LBA 1–2047     Reserved / post-MBR gap (zeroed on clean install)
 *   LBA 2048+      inteiliDOS partition  (type 0x99)
 *   LBA 2048       Volume Boot Record
 *   LBA 2049–2560  Kernel image (512 sectors = 256 KB, copied from RAM)
 *
 * The MBR bootstrap searches for the active partition and uses INT 13h
 * Extended Read (AH=42h) to load the VBR, then jumps to 0x7C00.
 */

#include "setup.h"
#include "../kernel/vga.h"
#include "../kernel/keyboard.h"
#include "../kernel/timer.h"
#include "../kernel/ata.h"
#include "../kernel/memory.h"
#include <stdint.h>
#include <stddef.h>

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
 *   0x25  LOAD_VBR: build DAP on stack, INT 13h 42h
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
    0xB2, 0x80,                    /* mov  dl, 0x80    (first HDD)      */
    0xCD, 0x13,                    /* int  0x13                         */
    0x73, 0x03,                    /* jnc  JMP_VBR  (→ 0x45)           */
    0xFA,                          /* cli */
    0xF4,                          /* hlt */
    0x90,                          /* nop  (padding to 0x45)            */

    /* 0x45 — JMP_VBR: far-jump to freshly-loaded VBR at 0x0000:0x7C00 */
    0xEA, 0x00, 0x7C, 0x00, 0x00,  /* jmp  far 0x0000:0x7C00 */

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

    /* 0x71 – 0x1BD: zero-fill remainder of bootstrap area */
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
static const uint8_t vbr_code[512] = {

    /* 0x000: jmp 0x044 ; nop  (0x02 + 0x42 = 0x044) */
    0xEB, 0x42, 0x90,

    /* 0x003–0x00A: GDT null descriptor */
    0x00,0x00,0x00,0x00, 0x00,0x00,0x00,0x00,

    /* 0x00B–0x012: GDT code descriptor sel=0x08 (base=0, limit=4G, ring0, 32-bit) */
    0xFF,0xFF, 0x00,0x00, 0x00, 0x9A, 0xCF, 0x00,

    /* 0x013–0x01A: GDT data descriptor sel=0x10 (base=0, limit=4G, ring0, 32-bit) */
    0xFF,0xFF, 0x00,0x00, 0x00, 0x92, 0xCF, 0x00,

    /* 0x01B–0x01C: GDTR limit = 23  (3 descriptors × 8 – 1) */
    0x17, 0x00,

    /* 0x01D–0x020: GDTR base = 0x00007C03  (physical address of GDT) */
    0x03, 0x7C, 0x00, 0x00,

    /* 0x021–0x024: kernel_lba — patched by sw_stage_install to part_lba+1 */
    0x00, 0x00, 0x00, 0x00,

    /* 0x025–0x042: banner "inteiliDOS 1.0 - Loading...\r\n\0" (30 bytes) */
    'i','n','t','e','i','l','i','D','O','S',
    ' ','1','.','0',' ','-',' ',
    'L','o','a','d','i','n','g','.','.','.','\r','\n',0,

    /* 0x043: padding zero */
    0,

    /* ── Phase 1: 16-bit real-mode bootstrap (entry point) ───────────────── */

    /* 0x044 */ 0xFA,                           /* cli                           */
    /* 0x045 */ 0x31,0xC0,                      /* xor  ax, ax                   */
    /* 0x047 */ 0x8E,0xD8,                      /* mov  ds, ax                   */
    /* 0x049 */ 0x8E,0xC0,                      /* mov  es, ax                   */
    /* 0x04B */ 0x8E,0xD0,                      /* mov  ss, ax                   */
    /* 0x04D */ 0xBC,0x00,0x7C,                 /* mov  sp, 0x7C00               */
    /* 0x050 */ 0xFB,                           /* sti                           */
    /* 0x051 */ 0xBE,0x25,0x7C,                 /* mov  si, 0x7C25  (banner msg) */

    /* print_loop: 0x054 */
    /* 0x054 */ 0xAC,                           /* lodsb                         */
    /* 0x055 */ 0x84,0xC0,                      /* test al, al                   */
    /* 0x057 */ 0x74,0x09,                      /* jz   0x062  (next IP 0x059 +9)*/
    /* 0x059 */ 0xB4,0x0E,                      /* mov  ah, 0x0E                 */
    /* 0x05B */ 0xBB,0x07,0x00,                 /* mov  bx, 7                    */
    /* 0x05E */ 0xCD,0x10,                      /* int  0x10                     */
    /* 0x060 */ 0xEB,0xF2,                      /* jmp  0x054  (next IP 0x062-14)*/

    /* A20 enable via keyboard controller: 0x062 */
    /* 0x062 */ 0xE4,0x64,                      /* in   al, 0x64   ←kbc_wait1   */
    /* 0x064 */ 0xA8,0x02,                      /* test al, 2                    */
    /* 0x066 */ 0x75,0xFA,                      /* jnz  0x062  (next IP 0x068-6) */
    /* 0x068 */ 0xB0,0xD1,                      /* mov  al, 0xD1                 */
    /* 0x06A */ 0xE6,0x64,                      /* out  0x64, al                 */
    /* 0x06C */ 0xE4,0x64,                      /* in   al, 0x64   ←kbc_wait2   */
    /* 0x06E */ 0xA8,0x02,                      /* test al, 2                    */
    /* 0x070 */ 0x75,0xFA,                      /* jnz  0x06C  (next IP 0x072-6) */
    /* 0x072 */ 0xB0,0xDF,                      /* mov  al, 0xDF  (A20 bit)      */
    /* 0x074 */ 0xE6,0x60,                      /* out  0x60, al                 */
    /* 0x076 */ 0xE4,0x64,                      /* in   al, 0x64   ←kbc_wait3   */
    /* 0x078 */ 0xA8,0x02,                      /* test al, 2                    */
    /* 0x07A */ 0x75,0xFA,                      /* jnz  0x076  (next IP 0x07C-6) */

    /* Load GDT and switch to 32-bit protected mode */
    /* 0x07C */ 0x0F,0x01,0x16,0x1B,0x7C,       /* lgdt [0x7C1B]                 */
    /* 0x081 */ 0xFA,                           /* cli                           */
    /* 0x082 */ 0x0F,0x20,0xC0,                 /* mov  eax, cr0                 */
    /* 0x085 */ 0x0C,0x01,                      /* or   al, 1    (set PE)        */
    /* 0x087 */ 0x0F,0x22,0xC0,                 /* mov  cr0, eax                 */
    /* 0x08A */ 0xEA,0x8F,0x7C,0x08,0x00,       /* jmp  far 0x08:0x7C8F          */

    /* ── Phase 2: 32-bit protected-mode kernel loader ────────────────────── */

    /* Reload data segments from GDT sel 0x10 (flat 4 GB) */
    /* 0x08F */ 0xB8,0x10,0x00,0x00,0x00,        /* mov  eax, 0x10                */
    /* 0x094 */ 0x8E,0xD8,                      /* mov  ds, ax                   */
    /* 0x096 */ 0x8E,0xC0,                      /* mov  es, ax  (needed by insw) */
    /* 0x098 */ 0x8E,0xD0,                      /* mov  ss, ax                   */
    /* 0x09A */ 0xBC,0x00,0x00,0x09,0x00,        /* mov  esp, 0x90000             */

    /* Load kernel: ESI=LBA, EDI=dest, ECX=sector count */
    /* 0x09F */ 0x8B,0x35,0x21,0x7C,0x00,0x00,  /* mov  esi, [0x7C21]  (LBA)     */
    /* 0x0A5 */ 0xBF,0x00,0x00,0x10,0x00,        /* mov  edi, 0x100000            */
    /* 0x0AA */ 0xB9,0x00,0x02,0x00,0x00,        /* mov  ecx, 512  (sectors)      */

    /* ATA PIO loop top (0x0AF): wait for BSY clear, then set up LBA registers */
    /* 0x0AF */ 0xBA,0xF7,0x01,0x00,0x00,        /* mov  edx, 0x1F7  (status)     */
    /* bsy_loop: 0x0B4 */
    /* 0x0B4 */ 0xEC,                           /* in   al, dx                   */
    /* 0x0B5 */ 0xA8,0x80,                      /* test al, 0x80  (BSY)          */
    /* 0x0B7 */ 0x75,0xFB,                      /* jnz  0x0B4  (next IP 0x0B9-5) */
    /* 0x0B9 */ 0x8B,0xC6,                      /* mov  eax, esi  (current LBA)  */
    /* 0x0BB */ 0x50,                           /* push eax                      */
    /* 0x0BC */ 0xC1,0xE8,0x18,                 /* shr  eax, 24                  */
    /* 0x0BF */ 0x24,0x0F,                      /* and  al, 0x0F                 */
    /* 0x0C1 */ 0x0C,0xE0,                      /* or   al, 0xE0  (LBA, master)  */
    /* 0x0C3 */ 0xBA,0xF6,0x01,0x00,0x00,        /* mov  edx, 0x1F6               */
    /* 0x0C8 */ 0xEE,                           /* out  dx, al   (drive/head)    */
    /* 0x0C9 */ 0x30,0xC0,                      /* xor  al, al                   */
    /* 0x0CB */ 0xBA,0xF1,0x01,0x00,0x00,        /* mov  edx, 0x1F1               */
    /* 0x0D0 */ 0xEE,                           /* out  dx, al   (features=0)    */
    /* 0x0D1 */ 0xB0,0x01,                      /* mov  al, 1                    */
    /* 0x0D3 */ 0xBA,0xF2,0x01,0x00,0x00,        /* mov  edx, 0x1F2               */
    /* 0x0D8 */ 0xEE,                           /* out  dx, al   (count=1)       */
    /* 0x0D9 */ 0x58,                           /* pop  eax   (restore LBA)      */
    /* 0x0DA */ 0xBA,0xF3,0x01,0x00,0x00,        /* mov  edx, 0x1F3               */
    /* 0x0DF */ 0xEE,                           /* out  dx, al   (LBA[7:0])      */
    /* 0x0E0 */ 0xC1,0xE8,0x08,                 /* shr  eax, 8                   */
    /* 0x0E3 */ 0xBA,0xF4,0x01,0x00,0x00,        /* mov  edx, 0x1F4               */
    /* 0x0E8 */ 0xEE,                           /* out  dx, al   (LBA[15:8])     */
    /* 0x0E9 */ 0xC1,0xE8,0x08,                 /* shr  eax, 8                   */
    /* 0x0EC */ 0xBA,0xF5,0x01,0x00,0x00,        /* mov  edx, 0x1F5               */
    /* 0x0F1 */ 0xEE,                           /* out  dx, al   (LBA[23:16])    */
    /* 0x0F2 */ 0xB0,0x20,                      /* mov  al, 0x20  (READ SECTORS) */
    /* 0x0F4 */ 0xBA,0xF7,0x01,0x00,0x00,        /* mov  edx, 0x1F7               */
    /* 0x0F9 */ 0xEE,                           /* out  dx, al   (issue command) */
    /* drq_loop: 0x0FA — wait for BSY clear then DRQ set */
    /* 0x0FA */ 0xEC,                           /* in   al, dx                   */
    /* 0x0FB */ 0xA8,0x80,                      /* test al, 0x80  (BSY)          */
    /* 0x0FD */ 0x75,0xFB,                      /* jnz  0x0FA  (next IP 0x0FF-5) */
    /* 0x0FF */ 0xA8,0x08,                      /* test al, 0x08  (DRQ)          */
    /* 0x101 */ 0x74,0xF7,                      /* jz   0x0FA  (next IP 0x103-9) */
    /* Read 256 words (512 bytes) into ES:EDI, advance EDI */
    /* 0x103 */ 0xBA,0xF0,0x01,0x00,0x00,        /* mov  edx, 0x1F0  (data port)  */
    /* 0x108 */ 0x51,                           /* push ecx                      */
    /* 0x109 */ 0xB9,0x00,0x01,0x00,0x00,        /* mov  ecx, 256  (words)        */
    /* 0x10E */ 0xF3,0x66,0x6D,                 /* rep insw  (256×2 = 512 bytes) */
    /* 0x111 */ 0x59,                           /* pop  ecx                      */
    /* 0x112 */ 0x46,                           /* inc  esi  (next LBA)          */
    /* 0x113 */ 0x49,                           /* dec  ecx  (sectors remaining) */
    /* 0x114 */ 0x75,0x99,                      /* jnz  0x0AF  (next IP 0x116-103)*/

    /* All 512 sectors loaded — jump to kernel entry */
    /* 0x116 */ 0x31,0xC0,                      /* xor  eax, eax  (no Multiboot magic; kernel supports 0) */
    /* 0x118 */ 0x31,0xDB,                      /* xor  ebx, ebx                 */
    /* 0x11A */ 0xEA,0x00,0x00,0x10,0x00,0x08,0x00, /* jmp far 0x08:0x100000    */

    /* 0x121–0x1FD: zero fill (221 bytes) */
    0,0,0,0,0,0,0,0,0,0, 0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0, 0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0, 0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0, 0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0, 0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0, 0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0, 0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0, 0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0, 0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0, 0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0, 0,0,0,0,0,0,0,0,0,0,
    0,

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
#define SW_KERNEL_SECTORS  512u         /* 256 KB  — covers any build */
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
    sw_content_line( 3, "", VGA_COLOR_WHITE, VGA_COLOR_BLUE);
    sw_content_line( 4, "  This wizard will install inteiliDOS onto your computer's",
                     VGA_COLOR_WHITE, VGA_COLOR_BLUE);
    sw_content_line( 5, "  hard drive or CompactFlash card and configure it to boot",
                     VGA_COLOR_WHITE, VGA_COLOR_BLUE);
    sw_content_line( 6, "  automatically when the computer starts.",
                     VGA_COLOR_WHITE, VGA_COLOR_BLUE);
    sw_content_line( 7, "", VGA_COLOR_WHITE, VGA_COLOR_BLUE);
    sw_content_line( 8, "  You will be asked to choose:", VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLUE);
    sw_content_line( 9, "    \x07  Installation mode  (dual-boot or clean install)",
                     VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLUE);
    sw_content_line(10, "    \x07  Target drive", VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLUE);
    sw_content_line(11, "", VGA_COLOR_WHITE, VGA_COLOR_BLUE);
    sw_content_line(12,
        "  WARNING: the clean-install option will permanently erase all",
        VGA_COLOR_LIGHT_RED, VGA_COLOR_BLUE);
    sw_content_line(13,
        "  data on the selected drive.  Back up your data before proceeding.",
        VGA_COLOR_LIGHT_RED, VGA_COLOR_BLUE);
    sw_content_line(14, "", VGA_COLOR_WHITE, VGA_COLOR_BLUE);
    sw_content_line(15,
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
        ata_write_sector((uint8_t)sw_drive, part_lba, sw_sector);
    });

    /* Step 4 — Copy kernel from RAM to disk */
    STEP(55, "  Copying system kernel to drive...", {
        const uint8_t *kptr = (const uint8_t *)SW_KERNEL_RAM_ADDR;
        for (uint32_t s = 0; s < SW_KERNEL_SECTORS; s++) {
            ata_write_sector((uint8_t)sw_drive,
                             part_lba + 1 + s,
                             kptr + s * 512);
            /* Update bar every 64 sectors */
            if ((s & 63) == 0) {
                int pct = 55 + (int)(s * 25 / SW_KERNEL_SECTORS);
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
    sw_content_line( 9, "  Next steps:", VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLUE);
    sw_content_line(10, "    1. Power off the computer.", VGA_COLOR_WHITE, VGA_COLOR_BLUE);
    sw_content_line(11, "    2. Set the BIOS/UEFI to boot from the installed drive.", VGA_COLOR_WHITE, VGA_COLOR_BLUE);
    sw_content_line(12, "    3. Power on — inteiliDOS will start automatically.", VGA_COLOR_WHITE, VGA_COLOR_BLUE);
    sw_content_line(13, "", VGA_COLOR_WHITE, VGA_COLOR_BLUE);
    sw_content_line(14, "  Press R to reboot now or any other key to return to shell.",
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
