/*
 * inteiliDOS -- shell/sam/sam_reciter.h
 * Bare-metal adaptation of SAM reciter (English text → SAM phoneme notation).
 */

#ifndef SAM_RECITER_H
#define SAM_RECITER_H

/*
 * Convert English text in input[] to SAM phoneme notation.
 * The buffer is modified in-place; it must already hold the text,
 * uppercase-normalised and terminated by 0x9B (155).
 * Returns 1 on success, 0 on failure.
 */
int TextToPhonemes(unsigned char *input);

#endif /* SAM_RECITER_H */
