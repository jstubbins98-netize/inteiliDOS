/*
 * inteilidOS -- kernel/vga.c
 * VGA text-mode (80x25) driver
 */

#include "vga.h"
#include <stdarg.h>
#include <stdint.h>

#define VGA_WIDTH  80
#define VGA_HEIGHT 25
#define VGA_MEMORY ((volatile uint16_t *)0xB8000)

/* I/O ports for VGA CRTC */
#define VGA_CTRL_REG 0x3D4
#define VGA_DATA_REG 0x3D5
#define VGA_CURSOR_HIGH 14
#define VGA_CURSOR_LOW  15

static int    vga_row;
static int    vga_col;
static uint8_t vga_attr;   /* current colour attribute byte */

/* ------------------------------------------------------------------------ */
static inline uint8_t make_attr(vga_color_t fg, vga_color_t bg) {
    return (uint8_t)((bg << 4) | fg);
}

static inline uint16_t make_entry(char c, uint8_t attr) {
    return (uint16_t)((uint16_t)attr << 8 | (uint8_t)c);
}

/* Low-level port I/O */
static inline void outb(uint16_t port, uint8_t val) {
    __asm__ volatile ("outb %0, %1" : : "a"(val), "Nd"(port));
}
static inline uint8_t inb(uint16_t port) {
    uint8_t val;
    __asm__ volatile ("inb %1, %0" : "=a"(val) : "Nd"(port));
    return val;
}

static void update_hw_cursor(void) {
    uint16_t pos = (uint16_t)(vga_row * VGA_WIDTH + vga_col);
    outb(VGA_CTRL_REG, VGA_CURSOR_HIGH);
    outb(VGA_DATA_REG, (uint8_t)(pos >> 8));
    outb(VGA_CTRL_REG, VGA_CURSOR_LOW);
    outb(VGA_DATA_REG, (uint8_t)(pos & 0xFF));
}

/* ------------------------------------------------------------------------ */
/*
 * vga_set_mode3 — switch display to VGA mode 3 (80×25 colour text) by
 * directly programming all VGA controller registers via port I/O.
 *
 * This must be called before vga_init() when the firmware leaves the display
 * in a graphical mode (e.g. the HP Vectra VEi8 Award BIOS may stay in its
 * splash-screen graphics mode when handing off to the MBR).  It does not use INT 10h at
 * all, so it works regardless of what state the BIOS left its ISR in.
 *
 * Register values are the canonical mode-3 values from the VGA specification
 * and OSDev documentation (80×25, 400-line, 16-colour, character height 16).
 */
void vga_set_mode3(void) {
    uint8_t i;

    /* Standard mode-3 register tables */
    static const uint8_t seq_regs[5]  = { 0x03, 0x00, 0x03, 0x00, 0x02 };
    static const uint8_t crtc_regs[25] = {
        0x5F, 0x4F, 0x50, 0x82, 0x55, 0x81, 0xBF, 0x1F,
        0x00, 0x4F, 0x0D, 0x0E, 0x00, 0x00, 0x00, 0x0F,
        0x9C, 0x0E, 0x8F, 0x28, 0x1F, 0x96, 0xB9, 0xA3, 0xFF
    };
    static const uint8_t gc_regs[9]   = {
        0x00, 0x00, 0x00, 0x00, 0x00, 0x10, 0x0E, 0x00, 0xFF
    };
    /* Attribute registers 0x00–0x14 (palette + mode control) */
    static const uint8_t attr_regs[21] = {
        0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x14, 0x07,
        0x38, 0x39, 0x3A, 0x3B, 0x3C, 0x3D, 0x3E, 0x3F,
        0x0C, 0x00, 0x0F, 0x08, 0x00
    };

    /* Miscellaneous Output: 25 MHz, colour, normal timing, page 0 */
    outb(0x3C2, 0x67);

    /* Sequencer */
    for (i = 0; i < 5; i++) { outb(0x3C4, i); outb(0x3C5, seq_regs[i]); }

    /* CRTC — clear bit 7 of index 0x11 first to unlock regs 0–7 */
    outb(0x3D4, 0x11);
    outb(0x3D5, (uint8_t)(inb(0x3D5) & 0x7F));
    for (i = 0; i < 25; i++) { outb(0x3D4, i); outb(0x3D5, crtc_regs[i]); }

    /* Graphics Controller */
    for (i = 0; i < 9; i++)  { outb(0x3CE, i); outb(0x3CF, gc_regs[i]); }

    /* Attribute Controller — read 0x3DA first to reset index/data flip-flop */
    (void)inb(0x3DA);
    for (i = 0; i < 21; i++) {
        outb(0x3C0, i);              /* index write (bit 5 = 0 → write mode) */
        outb(0x3C0, attr_regs[i]);   /* data write */
    }
    outb(0x3C0, 0x20);  /* re-enable display output (bit 5 = 1) */
}

void vga_init(void) {
    vga_row  = 0;
    vga_col  = 0;
    vga_attr = make_attr(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK);
    vga_clear();
}

void vga_set_color(vga_color_t fg, vga_color_t bg) {
    vga_attr = make_attr(fg, bg);
}

void vga_clear(void) {
    for (int y = 0; y < VGA_HEIGHT; y++)
        for (int x = 0; x < VGA_WIDTH; x++)
            VGA_MEMORY[y * VGA_WIDTH + x] = make_entry(' ', vga_attr);
    vga_row = 0;
    vga_col = 0;
    update_hw_cursor();
}

void vga_scroll(void) {
    /* Move all rows up by one */
    for (int y = 1; y < VGA_HEIGHT; y++)
        for (int x = 0; x < VGA_WIDTH; x++)
            VGA_MEMORY[(y - 1) * VGA_WIDTH + x] = VGA_MEMORY[y * VGA_WIDTH + x];
    /* Blank last row */
    for (int x = 0; x < VGA_WIDTH; x++)
        VGA_MEMORY[(VGA_HEIGHT - 1) * VGA_WIDTH + x] = make_entry(' ', vga_attr);
    vga_row = VGA_HEIGHT - 1;
}

void vga_set_cursor(int row, int col) {
    vga_row = row;
    vga_col = col;
    update_hw_cursor();
}

void vga_get_cursor(int *row, int *col) {
    *row = vga_row;
    *col = vga_col;
}

void vga_put_colored(char c, vga_color_t fg, vga_color_t bg) {
    uint8_t saved = vga_attr;
    vga_attr = make_attr(fg, bg);
    vga_putchar(c);
    vga_attr = saved;
}

void vga_putchar(char c) {
    if (c == '\n') {
        vga_col = 0;
        vga_row++;
    } else if (c == '\r') {
        vga_col = 0;
    } else if (c == '\t') {
        vga_col = (vga_col + 8) & ~7;
        if (vga_col >= VGA_WIDTH) {
            vga_col = 0;
            vga_row++;
        }
    } else if (c == '\b') {
        if (vga_col > 0) {
            vga_col--;
            VGA_MEMORY[vga_row * VGA_WIDTH + vga_col] = make_entry(' ', vga_attr);
        }
    } else {
        VGA_MEMORY[vga_row * VGA_WIDTH + vga_col] = make_entry(c, vga_attr);
        vga_col++;
        if (vga_col >= VGA_WIDTH) {
            vga_col = 0;
            vga_row++;
        }
    }
    if (vga_row >= VGA_HEIGHT)
        vga_scroll();
    update_hw_cursor();
}

void vga_puts(const char *str) {
    while (*str)
        vga_putchar(*str++);
}

/* ---- Minimal printf ---- */
/* Use uint32_t to avoid 64-bit division helpers (__udivmoddi4) on i686. */
static void print_uint32_w(uint32_t v, uint32_t base, int width, int zero_pad) {
    static const char digits[] = "0123456789ABCDEF";
    char buf[32];
    int  i = 0;
    if (v == 0) { buf[i++] = '0'; }
    else { while (v) { buf[i++] = digits[v % base]; v /= base; } }
    /* pad to width */
    char pad = zero_pad ? '0' : ' ';
    while (i < width) buf[i++] = pad;
    while (--i >= 0) vga_putchar(buf[i]);
}

void vga_printf(const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    for (const char *p = fmt; *p; p++) {
        if (*p != '%') { vga_putchar(*p); continue; }
        p++;
        /* Parse optional flags and width: e.g. %02X, %8u, %d */
        int zero_pad = 0, width = 0;
        if (*p == '0') { zero_pad = 1; p++; }
        while (*p >= '1' && *p <= '9') { width = width * 10 + (*p - '0'); p++; }
        switch (*p) {
            case 'd': {
                int v = va_arg(ap, int);
                if (v < 0) { vga_putchar('-'); v = -v; }
                print_uint32_w((uint32_t)v, 10, width, zero_pad);
                break;
            }
            case 'u': print_uint32_w((uint32_t)va_arg(ap, unsigned), 10, width, zero_pad); break;
            case 'x':
            case 'X': print_uint32_w((uint32_t)va_arg(ap, unsigned), 16, width, zero_pad); break;
            case 'p': vga_puts("0x"); print_uint32_w((uint32_t)(uintptr_t)va_arg(ap, void*), 16, 0, 0); break;
            case 's': { const char *s = va_arg(ap, const char*); vga_puts(s ? s : "(null)"); break; }
            case 'c': vga_putchar((char)va_arg(ap, int)); break;
            case '%': vga_putchar('%'); break;
            default:  vga_putchar('%'); vga_putchar(*p); break;
        }
    }
    va_end(ap);
}
