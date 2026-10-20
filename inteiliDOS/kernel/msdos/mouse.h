#ifndef MSDOS_MOUSE_H
#define MSDOS_MOUSE_H
#include <stdint.h>
#define MSDOS_MOUSE_ENTRY 0x3000
#define MSDOS_MOUSE_RETURN 0x3010
int msdos_mouse_start(int requested);
int msdos_mouse_enabled(void);
/* Host display gate only: does not disable DOS mouse input/services. */
void msdos_mouse_toggle_cursor(void);
void msdos_mouse_stop(void);
void msdos_mouse_install(uint8_t *ram);
void msdos_mouse_poll(uint8_t mode);
void msdos_mouse_bios(uint16_t *registers, const uint8_t *ram);
int msdos_mouse_event(uint16_t *registers, uint16_t *segment, uint16_t *offset);
uint16_t msdos_mouse_text(unsigned cell, uint16_t value);
uint8_t msdos_mouse_pixel(unsigned x, unsigned y, unsigned width, uint8_t value);
#endif
