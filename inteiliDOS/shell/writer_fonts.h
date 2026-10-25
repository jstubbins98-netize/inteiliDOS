#ifndef WRITER_FONTS_H
#define WRITER_FONTS_H
#include <stdint.h>
/* On-demand state; no permanent kernel BSS for the font snapshot. */
typedef struct {
    uint8_t plane[65536];
    uint8_t map, palette8, mode_control;
} writer_font_state_t;
void writer_fonts_begin(writer_font_state_t *state);
void writer_fonts_end(const writer_font_state_t *state);
#endif
