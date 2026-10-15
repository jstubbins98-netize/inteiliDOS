#ifndef SAM_PHONEME_H
#define SAM_PHONEME_H

/*
 * inteiliDOS -- shell/sam/sam_phoneme.h
 * Public API for the SAM phoneme parser + PCM pipeline driver.
 *
 * Usage:
 *   1. sam_set_input(text)         — load C string into sam_input[]
 *   2. TextToPhonemes(sam_input)   — English text → SAM notation (reciter)
 *   3. SAMMain()                   — notation → PCM in sam_pcm_buf[]
 *   4. speaker_play_pcm(sam_pcm_buf, sam_pcm_len)  — play through speaker
 *
 * sam_pcm_buf and sam_pcm_len are declared in sam_render.h.
 */

/* Input buffer (unsigned char so it matches TextToPhonemes' parameter type).
 * sam_set_input() fills this; TextToPhonemes() converts it in-place. */
extern unsigned char sam_input[256];

/* Run the full SAM phoneme pipeline.
 * On success fills sam_pcm_buf[] / sam_pcm_len and returns 1.
 * On failure returns 0. */
int SAMMain(void);

/* Fill sam_input[] from a C string (upper-cases + adds 0x9B terminator). */
void sam_set_input(const char *text);

/* Optional voice parameter setters (must be called before SAMMain). */
void SetSpeed(unsigned char speed);   /* higher = slower; default 72  */
void SetPitch(unsigned char pitch);   /* 0-255; default 64             */
void SetMouth(unsigned char mouth);   /* 0-255; default 128            */
void SetThroat(unsigned char throat); /* 0-255; default 128            */

#endif /* SAM_PHONEME_H */
