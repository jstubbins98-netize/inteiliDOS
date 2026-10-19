#ifndef MSDOS_SOUND_H
#define MSDOS_SOUND_H
#include <stdint.h>
/* Isolated SB playback/DMA/PIC model. Does not access native hardware. */
void msdos_sound_reset(int enabled,const uint8_t *ram,unsigned size);
int msdos_sound_in(unsigned port,uint8_t *value);
int msdos_sound_out(unsigned port,uint8_t value);
void msdos_sound_render(int16_t *stereo,unsigned frames);
int msdos_sound_irq(void);
int msdos_sound_irq_pending(void);
int msdos_sound_error(void);
#endif
