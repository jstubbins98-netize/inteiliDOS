#ifndef MSDOS_SOUND_H
#define MSDOS_SOUND_H
#include <stdint.h>
/* Isolated SB PCM + OPL playback/DMA/PIC. No native hardware access. */
enum {MSDOS_SOUND_OFF=0,MSDOS_SOUND_SB2=1,MSDOS_SOUND_SB16=2};
void msdos_sound_select(unsigned model);
unsigned msdos_sound_model(void);
void msdos_sound_reset(unsigned model,uint8_t *ram,unsigned size);
void msdos_sound_stop(void);
int msdos_sound_in(unsigned port,uint8_t *value);
int msdos_sound_out(unsigned port,uint8_t value);
void msdos_sound_render(int16_t *stereo,unsigned frames);
int msdos_sound_irq(void);
int msdos_sound_irq_pending(void);
int msdos_sound_error(void);
#endif
