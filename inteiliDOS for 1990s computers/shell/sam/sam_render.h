/*
 * inteiliDOS -- shell/sam/sam_render.h
 * Public API for the SAM formant synthesiser (sam_render.c).
 */
#ifndef SAM_RENDER_H
#define SAM_RENDER_H

#include <stdint.h>

/* PCM output: 8-bit unsigned, ~22050 Hz, filled by SAMMain()/Render(). */
#define SAM_PCM_RATE 22050u
extern unsigned char sam_pcm_buf[];   /* raw sample data */
extern int           sam_pcm_len;     /* number of valid bytes in sam_pcm_buf */

/* Called from Init() (sam_phoneme.c) to reset the PCM cursor. */
void sam_render_reset(void);

/* Called from PrepareOutput() in sam_phoneme.c once per breath group. */
void Render(void);

/* Recalculates formant filter tables for the given mouth/throat settings.
 * Called from Init() in sam_phoneme.c. */
void SetMouthThroat(unsigned char mouth, unsigned char throat);

#endif /* SAM_RENDER_H */
