/* Text-mode VGA bold/italic glyphs. Both font banks and all changed registers
 * are restored before returning to the shell. No BIOS calls or graphics mode. */
#include "writer_fonts.h"

static void outb(uint16_t port, uint8_t value) {
    __asm__ volatile ("outb %0, %1" : : "a"(value), "Nd"(port));
}
static uint8_t inb(uint16_t port) {
    uint8_t value;
    __asm__ volatile ("inb %1, %0" : "=a"(value) : "Nd"(port));
    return value;
}
static uint8_t read_reg(uint16_t port, uint8_t index) {
    outb(port, index); return inb(port + 1);
}
static void write_reg(uint16_t port, uint8_t index, uint8_t value) {
    outb(port, index); outb(port + 1, value);
}
static uint8_t palette_read(uint8_t index) {
    (void)inb(0x3DA); outb(0x3C0, index | 0x20);
    uint8_t result = inb(0x3C1);
    (void)inb(0x3DA); outb(0x3C0, 0x20);
    return result;
}
static void palette_write(uint8_t index, uint8_t value) {
    (void)inb(0x3DA); outb(0x3C0, index);
    outb(0x3C0, value); outb(0x3C0, 0x20);
}
typedef struct { uint8_t seq_index, gc_index, mask, memory, read, mode, misc; } mapping_t;
static mapping_t map_font(void) {
    mapping_t m;
    m.seq_index = inb(0x3C4); m.gc_index = inb(0x3CE);
    m.mask = read_reg(0x3C4, 2); m.memory = read_reg(0x3C4, 4);
    m.read = read_reg(0x3CE, 4); m.mode = read_reg(0x3CE, 5); m.misc = read_reg(0x3CE, 6);
    write_reg(0x3C4, 2, 4); write_reg(0x3C4, 4, 7);
    write_reg(0x3CE, 4, 2); write_reg(0x3CE, 5, 0); write_reg(0x3CE, 6, 4);
    return m;
}
static void unmap_font(mapping_t m) {
    write_reg(0x3C4, 2, m.mask); write_reg(0x3C4, 4, m.memory);
    write_reg(0x3CE, 4, m.read); write_reg(0x3CE, 5, m.mode); write_reg(0x3CE, 6, m.misc);
    outb(0x3C4, m.seq_index); outb(0x3CE, m.gc_index);
}
void writer_fonts_begin(writer_font_state_t *state) {
    uint8_t seq_index = inb(0x3C4), crtc_index = inb(0x3D4);
    state->map = read_reg(0x3C4, 3);
    unsigned height = (read_reg(0x3D4, 9) & 31) + 1;
    outb(0x3D4, crtc_index);
    state->palette8 = palette_read(8);
    unsigned bank = (state->map & 3) | ((state->map >> 2) & 4);
    unsigned source = (bank & 3) * 16384 + (bank & 4) * 2048;
    mapping_t m = map_font();
    volatile uint8_t *plane = (volatile uint8_t *)0xA0000;
    for (unsigned i = 0; i < 65536; i++) state->plane[i] = plane[i];
    for (unsigned i = 0; i < 8192; i++) {
        plane[i] = state->plane[source + i];
        plane[16384 + i] = state->plane[source + i];
    }
    for (unsigned c = 32; c < 127; c++) for (unsigned y = 0; y < 32; y++) {
        uint8_t normal = state->plane[source + c * 32 + y], italic = normal;
        if (y < height / 3) italic >>= 1;
        else if (y >= height * 2 / 3 && y < height) italic <<= 1;
        plane[(c + 96) * 32 + y] = normal | (normal >> 1);
        plane[16384 + c * 32 + y] = italic;
        plane[16384 + (c + 96) * 32 + y] = italic | (italic >> 1);
    }
    unmap_font(m);
    /* Attribute bit 3 selects font A (bank 1); clear selects B (bank 0).
     * Match palette 8 to black so italic text retains the same ink color. */
    write_reg(0x3C4, 3, 4); outb(0x3C4, seq_index);
    palette_write(8, palette_read(0));
}
void writer_fonts_end(const writer_font_state_t *state) {
    uint8_t seq_index = inb(0x3C4);
    mapping_t m = map_font();
    volatile uint8_t *plane = (volatile uint8_t *)0xA0000;
    for (unsigned i = 0; i < 65536; i++) plane[i] = state->plane[i];
    unmap_font(m);
    write_reg(0x3C4, 3, state->map); outb(0x3C4, seq_index);
    palette_write(8, state->palette8);
}
