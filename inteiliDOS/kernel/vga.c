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

static void update_hw_cursor(void) {
    uint16_t pos = (uint16_t)(vga_row * VGA_WIDTH + vga_col);
    outb(VGA_CTRL_REG, VGA_CURSOR_HIGH);
    outb(VGA_DATA_REG, (uint8_t)(pos >> 8));
    outb(VGA_CTRL_REG, VGA_CURSOR_LOW);
    outb(VGA_DATA_REG, (uint8_t)(pos & 0xFF));
}

/* ------------------------------------------------------------------------ */
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
static void print_uint32(uint32_t v, uint32_t base) {
    static const char digits[] = "0123456789ABCDEF";
    char buf[32];
    int  i = 0;
    if (v == 0) { vga_putchar('0'); return; }
    while (v) { buf[i++] = digits[v % base]; v /= base; }
    while (--i >= 0) vga_putchar(buf[i]);
}

void vga_printf(const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    for (const char *p = fmt; *p; p++) {
        if (*p != '%') { vga_putchar(*p); continue; }
        p++;
        switch (*p) {
            case 'd': {
                int v = va_arg(ap, int);
                if (v < 0) { vga_putchar('-'); v = -v; }
                print_uint32((uint32_t)v, 10);
                break;
            }
            case 'u': print_uint32((uint32_t)va_arg(ap, unsigned), 10); break;
            case 'x': print_uint32((uint32_t)va_arg(ap, unsigned), 16); break;
            case 'p': vga_puts("0x"); print_uint32((uint32_t)(uintptr_t)va_arg(ap, void*), 16); break;
            case 's': { const char *s = va_arg(ap, const char*); vga_puts(s ? s : "(null)"); break; }
            case 'c': vga_putchar((char)va_arg(ap, int)); break;
            case '%': vga_putchar('%'); break;
            default:  vga_putchar('%'); vga_putchar(*p); break;
        }
    }
    va_end(ap);
}
