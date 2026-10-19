#ifndef MSDOS_VIDEO_H
#define MSDOS_VIDEO_H
#include <stdint.h>
/* Entirely private guest VGA. No guest register is sent to physical hardware. */
int msdos_video_reset(uint8_t *ram);
void msdos_video_release(void);
void msdos_video_font(const uint8_t *font);
int msdos_video_active(void);
int msdos_video_mapped(uint32_t address);
uint8_t msdos_video_read(uint32_t address);
void msdos_video_write(uint32_t address, uint8_t value);
int msdos_video_in(uint16_t port, uint8_t *value);
int msdos_video_out(uint16_t port, uint8_t value);
int msdos_video_bios(uint16_t *registers);
unsigned msdos_video_width(void);
unsigned msdos_video_height(void);
uint8_t msdos_video_pixel(unsigned x, unsigned y);
const uint8_t *msdos_video_palette(void);
uint32_t msdos_video_palette_version(void);
#endif
