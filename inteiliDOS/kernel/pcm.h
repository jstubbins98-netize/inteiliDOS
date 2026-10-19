#ifndef PCM_OUTPUT_H
#define PCM_OUTPUT_H
#include <stdint.h>
/* Poll-driven 48 kHz signed 16-bit stereo playback, bounded native DMA only. */
int pcm_start(void (*render)(int16_t *,unsigned));
void pcm_poll(void);
void pcm_stop(void);
const char *pcm_backend(void);
const char *pcm_error(void);
#endif
