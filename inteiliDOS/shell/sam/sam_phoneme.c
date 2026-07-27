/*
 * inteiliDOS -- shell/sam/sam_phoneme.c
 * Bare-metal adaptation of SAM (Software Automatic Mouth) phoneme parser
 * and PCM pipeline driver.
 *
 * Derived from SAM-master (C port of the C64 SAM by Don't Ask Software).
 * Bare-metal adaptations:
 *   - All #include <stdio.h/stdlib.h/string.h> removed.
 *   - malloc()/free() replaced by static sam_pcm_buf[] in sam_render.c.
 *   - All `if (debug) printf(...)` blocks removed.
 *   - SetMouthThroat() implemented in sam_render.c (full formant version).
 *   - PrepareOutput() and the full Render() pipeline restored.
 *
 * Public API (see sam_phoneme.h):
 *   void  sam_set_input(const char *text)  — fill sam_input[] from C string
 *   int   SAMMain(void)                    — run full pipeline → sam_pcm_buf
 *   void  SetSpeed/Pitch/Mouth/Throat()    — optional parameter tweaks
 */

#include "sam_phoneme.h"
#include "sam_render.h"
#include "SamTabs.h"

/* ── Public globals ─────────────────────────────────────────────────────── */

/* Input buffer.  sam_set_input() writes here; TextToPhonemes() converts it
 * in-place to SAM phoneme notation; Parser1() reads the result. */
unsigned char sam_input[256];

/* Raw phoneme arrays from the parser chain (255-terminated). */
unsigned char sam_phonemeindex[256];
unsigned char sam_phonemeLength[256];

/* 6502 register emulation (shared with sam_reciter.c and sam_render.c) */
unsigned char A, X, Y;

/* ── Shared globals used by sam_render.c ────────────────────────────────── */
unsigned char mem39 = 0;    /* sampled consonant flag copy              */
unsigned char mem44 = 0;    /* phoneme loop counter                     */
unsigned char mem47 = 0;    /* parameter table index (168-174)          */
unsigned char mem49 = 0;    /* frame position tracker                   */
unsigned char mem50 = 0;    /* sign flag for interpolation division     */
unsigned char mem51 = 0;    /* remainder for interpolation division     */
unsigned char mem53 = 0;    /* interpolation delta value                */
unsigned char mem56 = 0;    /* multi-purpose scratch (AdjustLengths +
                               render interpolation + RenderSample)     */

/* SAM voice parameters */
unsigned char speed   = 72;   /* frames per output cycle (higher = slower) */
unsigned char pitch   = 64;   /* fundamental pitch (0-255)                 */
int           singmode = 0;   /* 1 = disable pitch contour adjustment      */

/* Render input arrays — filled by PrepareOutput(), consumed by Render() */
unsigned char phonemeIndexOutput[60];
unsigned char stressOutput[60];
unsigned char phonemeLengthOutput[60];

/* ── Private state ──────────────────────────────────────────────────────── */
static unsigned char mouth  = 128;
static unsigned char throat = 128;
static unsigned char mem59  = 0;   /* default phoneme length for inserted phonemes */
static unsigned char stress[256];

/* ── Parameter setters ──────────────────────────────────────────────────── */
void SetSpeed(unsigned char s)  { speed  = s; }
void SetPitch(unsigned char p)  { pitch  = p; }
void SetMouth(unsigned char m)  { mouth  = m; }
void SetThroat(unsigned char t) { throat = t; }

/* ── sam_set_input ────────────────────────────────────────────────────────
 * Copy text into sam_input[], upper-casing ASCII letters, and append the
 * SAM end-of-buffer marker 0x9B (155).  TextToPhonemes() converts
 * sam_input[] in-place to SAM phoneme notation before SAMMain() is called.
 */
void sam_set_input(const char *text) {
    int i = 0;
    while (text[i] && i < 254) {
        unsigned char c = (unsigned char)text[i];
        if (c >= 'a' && c <= 'z') c = (unsigned char)(c - 32);
        sam_input[i] = c;
        i++;
    }
    sam_input[i] = 155; /* 0x9B end-of-buffer marker */
}

/* ── Forward declarations ─────────────────────────────────────────────── */
static void Parser2(void);
static void CopyStress(void);
static void SetPhonemeLength(void);
static void AdjustLengths(void);
static void Code41240(void);
static void Insert(unsigned char position, unsigned char mem60,
                   unsigned char mem59val, unsigned char mem58);
static void InsertBreath(void);
static int  Parser1(void);

/* ── PrepareOutput ────────────────────────────────────────────────────────
 * Walks sam_phonemeindex[], splitting at breath boundaries (254).
 * For each breath group, fills phonemeIndexOutput[]/stressOutput[]/
 * phonemeLengthOutput[] and calls Render() to append PCM to sam_pcm_buf.
 */
static void PrepareOutput(void) {
    A = 0; X = 0; Y = 0;
    while (1) {
        A = sam_phonemeindex[X];
        if (A == 255) {
            phonemeIndexOutput[Y] = 255;
            Render();
            return;
        }
        if (A == 254) {
            X++;
            int saved_x = X;
            phonemeIndexOutput[Y] = 255;
            Render();
            X = (unsigned char)saved_x;
            Y = 0;
            continue;
        }
        if (A == 0) { X++; continue; }
        phonemeIndexOutput[Y]  = A;
        phonemeLengthOutput[Y] = sam_phonemeLength[X];
        stressOutput[Y]        = stress[X];
        X++;
        Y++;
    }
}

/* ── Init ─────────────────────────────────────────────────────────────── */
static void Init(void) {
    int i;
    SetMouthThroat(mouth, throat);
    sam_render_reset();
    for (i = 0; i < 256; i++) {
        stress[i]            = 0;
        sam_phonemeLength[i] = 0;
    }
    for (i = 0; i < 60; i++) {
        phonemeIndexOutput[i]  = 0;
        stressOutput[i]        = 0;
        phonemeLengthOutput[i] = 0;
    }
    sam_phonemeindex[255] = 255;
}

/* ── SAMMain ──────────────────────────────────────────────────────────── */
int SAMMain(void) {
    Init();
    sam_phonemeindex[255] = 32; /* guard against buffer overflow */

    if (!Parser1()) return 0;

    Parser2();
    CopyStress();
    SetPhonemeLength();
    AdjustLengths();
    Code41240();

    /* Clamp any out-of-range phoneme indices */
    X = 0;
    do {
        A = sam_phonemeindex[X];
        if (A > 80) { sam_phonemeindex[X] = 255; break; }
        X++;
    } while (X != 0);

    InsertBreath();

    /* Run the full PCM render pipeline → sam_pcm_buf / sam_pcm_len */
    PrepareOutput();
    return 1;
}

/* ── Insert ──────────────────────────────────────────────────────────────── */
static void Insert(unsigned char position, unsigned char mem60,
                   unsigned char mem59val, unsigned char mem58) {
    for (int i = 253; i >= position; i--) {
        sam_phonemeindex[i + 1]  = sam_phonemeindex[i];
        sam_phonemeLength[i + 1] = sam_phonemeLength[i];
        stress[i + 1]            = stress[i];
    }
    sam_phonemeindex[position]  = mem60;
    sam_phonemeLength[position] = mem59val;
    stress[position]            = mem58;
}

/* ── InsertBreath ────────────────────────────────────────────────────────── */
static void InsertBreath(void) {
    unsigned char mem54 = 255;
    unsigned char mem55 = 0;
    unsigned char mem66 = 0;
    unsigned char index;
    X++;
    while (1) {
        X     = mem66;
        index = sam_phonemeindex[X];
        if (index == 255) return;
        mem55 += sam_phonemeLength[X];
        if (mem55 < 232) {
            if (index != 254) {
                A = flags2[index] & 1;
                if (A != 0) {
                    X++;
                    mem55 = 0;
                    Insert(X, 254, mem59, 0);
                    mem66++;
                    mem66++;
                    continue;
                }
            }
            if (index == 0) mem54 = X;
            mem66++;
            continue;
        }
        X = mem54;
        sam_phonemeindex[X]  = 31; /* Q* glottal stop */
        sam_phonemeLength[X] = 4;
        stress[X]            = 0;
        X++;
        mem55 = 0;
        Insert(X, 254, mem59, 0);
        X++;
        mem66 = X;
    }
}

/* ── CopyStress ──────────────────────────────────────────────────────────── */
static void CopyStress(void) {
    unsigned char pos = 0;
    while (1) {
        Y = sam_phonemeindex[pos];
        if (Y == 255) return;
        if ((flags[Y] & 64) == 0) { pos++; continue; }
        Y = sam_phonemeindex[pos + 1];
        if (Y == 255)              { pos++; continue; }
        if ((flags[Y] & 128) == 0) { pos++; continue; }
        Y = stress[pos + 1];
        if (Y == 0)         { pos++; continue; }
        if ((Y & 128) != 0) { pos++; continue; }
        stress[pos] = Y + 1;
        pos++;
    }
}

/* ── Parser1 ─────────────────────────────────────────────────────────────── */
static int Parser1(void) {
    unsigned char sign1;
    unsigned char sign2;
    unsigned char position = 0;

    X = 0; A = 0; Y = 0;
    for (int i = 0; i < 256; i++) stress[i] = 0;

    while (1) {
        sign1 = (unsigned char)sam_input[X];
        if (sign1 == 155) {          /* end-of-buffer marker */
            sam_phonemeindex[position] = 255;
            return 1;
        }
        X++;
        sign2 = (unsigned char)sam_input[X];

        /* Try 2-character match (exact second character) */
        Y = 0;
pos41095:
        A = signInputTable1[Y];
        if (A == sign1) {
            A = signInputTable2[Y];
            if ((A != '*') && (A == sign2)) {
                sam_phonemeindex[position] = Y;
                position++;
                X++;
                continue;
            }
        }
        Y++;
        if (Y != 81) goto pos41095;

        /* Try 1-character wildcard match */
        Y = 0;
pos41134:
        if (signInputTable2[Y] == '*') {
            if (signInputTable1[Y] == sign1) {
                sam_phonemeindex[position] = Y;
                position++;
                continue;
            }
        }
        Y++;
        if (Y != 81) goto pos41134;

        /* Try stress table match */
        Y = 8;
        while ((sign1 != stressInputTable[Y]) && (Y > 0)) Y--;
        if (Y == 0) return 0;
        stress[position - 1] = Y;
    }
}

/* ── SetPhonemeLength ────────────────────────────────────────────────────── */
static void SetPhonemeLength(void) {
    unsigned char Av;
    int position = 0;
    while (sam_phonemeindex[position] != 255) {
        Av = stress[position];
        if ((Av == 0) || ((Av & 128) != 0))
            sam_phonemeLength[position] = phonemeLengthTable[sam_phonemeindex[position]];
        else
            sam_phonemeLength[position] = phonemeStressedLengthTable[sam_phonemeindex[position]];
        position++;
    }
}

/* ── Code41240 ───────────────────────────────────────────────────────────── */
static void Code41240(void) {
    unsigned char pos = 0;
    while (sam_phonemeindex[pos] != 255) {
        unsigned char index;
        X     = pos;
        index = sam_phonemeindex[pos];
        if ((flags[index] & 2) == 0) { pos++; continue; }
        if ((flags[index] & 1) == 0) {
            Insert(pos + 1, index + 1, phonemeLengthTable[index + 1], stress[pos]);
            Insert(pos + 2, index + 2, phonemeLengthTable[index + 2], stress[pos]);
            pos += 3;
            continue;
        }
        do { X++; A = sam_phonemeindex[X]; } while (A == 0);
        if (A != 255) {
            if ((flags[A] & 8) != 0)         { pos++; continue; }
            if ((A == 36) || (A == 37))       { pos++; continue; }
        }
        Insert(pos + 1, index + 1, phonemeLengthTable[index + 1], stress[pos]);
        Insert(pos + 2, index + 2, phonemeLengthTable[index + 2], stress[pos]);
        pos += 3;
    }
}

/* ── Parser2 ─────────────────────────────────────────────────────────────── */
static void Parser2(void) {
    unsigned char pos  = 0;
    unsigned char mem58 = 0;

    while (1) {
        X = pos;
        A = sam_phonemeindex[pos];
        if (A == 0)   { pos++; continue; }
        if (A == 255) return;
        Y = A;

        /* Diphthong: append WX (20) or YX (21) */
        if ((flags[A] & 16) == 0) goto pos41457;
        mem58 = stress[pos];
        A = flags[Y] & 32;
        if (A == 0) A = 20; else A = 21;
        Insert(pos + 1, A, mem59, mem58);
        X = pos;
        goto pos41749;

pos41457:
        A = sam_phonemeindex[X];
        if (A != 78) goto pos41487;   /* UL → AX L */
        A = 24;                        /* L */
pos41466:
        mem58 = stress[X];
        sam_phonemeindex[X] = 13;      /* AX */
        Insert(X + 1, A, mem59, mem58);
        pos++; continue;

pos41487:
        if (A != 79) goto pos41495;   /* UM → AX M */
        A = 27; goto pos41466;
pos41495:
        if (A != 80) goto pos41503;   /* UN → AX N */
        A = 28; goto pos41466;

pos41503:
        Y = A;
        A = flags[A] & 128;
        if (A != 0) {
            A = stress[X];
            if (A != 0) {
                X++;
                A = sam_phonemeindex[X];
                if (A == 0) {
                    X++;
                    Y = sam_phonemeindex[X];
                    A = (Y == 255) ? (unsigned char)(65 & 128) : (unsigned char)(flags[Y] & 128);
                    if (A != 0) {
                        A = stress[X];
                        if (A != 0) {
                            Insert(X, 31, mem59, 0); /* Q* glottal stop */
                            pos++; continue;
                        }
                    }
                }
            }
        }

        X = pos;
        A = sam_phonemeindex[pos];
        if (A != 23) goto pos41611;   /* R: T R → CH R,  D R → J R,  vowel R → RX */
        X--;
        A = sam_phonemeindex[pos - 1];
        if (A == 69) { sam_phonemeindex[pos - 1] = 42; goto pos41779; } /* T→CH */
        if (A == 57) { sam_phonemeindex[pos - 1] = 44; goto pos41788; } /* D→J  */
        A = flags[A] & 128;
        if (A != 0) sam_phonemeindex[pos] = 18; /* R→RX */
        pos++; continue;

pos41611:
        if (A == 24) { /* L→LX after vowel */
            if ((flags[sam_phonemeindex[pos - 1]] & 128) == 0) { pos++; continue; }
            sam_phonemeindex[X] = 19;
            pos++; continue;
        }
        if (A == 32) { /* G S → G Z */
            if (sam_phonemeindex[pos - 1] != 60) { pos++; continue; }
            sam_phonemeindex[pos] = 38;
            pos++; continue;
        }
        if (A == 72) { /* K → KX before non-IY vowel/diphthong */
            Y = sam_phonemeindex[pos + 1];
            if (Y == 255) sam_phonemeindex[pos] = 75;
            else { if ((flags[Y] & 32) == 0) sam_phonemeindex[pos] = 75; }
        } else if (A == 60) { /* G → GX */
            unsigned char idx = sam_phonemeindex[pos + 1];
            if (idx == 255)              { pos++; continue; }
            if ((flags[idx] & 32) != 0) { pos++; continue; }
            sam_phonemeindex[pos] = 63;
            pos++; continue;
        }

        Y = sam_phonemeindex[pos];
        A = flags[Y] & 1;
        if (A == 0) goto pos41749;
        A = sam_phonemeindex[pos - 1];
        if (A != 32) { A = Y; goto pos41812; }
        sam_phonemeindex[pos] = Y - 12; /* S* soften */
        pos++; continue;

pos41749:
        A = sam_phonemeindex[X];
        if (A == 53) { /* UW → UX after alveolar */
            Y = sam_phonemeindex[X - 1];
            A = flags2[Y] & 4;
            if (A == 0) { pos++; continue; }
            sam_phonemeindex[X] = 16;
            pos++; continue;
        }
pos41779:
        if (A == 42) { /* CH → CH CH+1 */
            Insert(X + 1, A + 1, mem59, stress[X]);
            pos++; continue;
        }
pos41788:
        if (A == 44) { /* J → J J+1 */
            Insert(X + 1, A + 1, mem59, stress[X]);
            pos++; continue;
        }
pos41812:
        if (A != 69)        /* T */
        if (A != 57) { pos++; continue; } /* D */
        if ((flags[sam_phonemeindex[X - 1]] & 128) == 0) { pos++; continue; }
        X++;
        A = sam_phonemeindex[X];
        if (A != 0) {
            if ((flags[A] & 128) == 0) { pos++; continue; }
            if (stress[X] != 0)        { pos++; continue; }
            sam_phonemeindex[pos] = 30; /* DX */
        } else {
            A = sam_phonemeindex[X + 1];
            A = (A == 255) ? (unsigned char)(65 & 128) : (unsigned char)(flags[A] & 128);
            if (A != 0) sam_phonemeindex[pos] = 30;
        }
        pos++;
    }
}

/* ── AdjustLengths ───────────────────────────────────────────────────────── */
static void AdjustLengths(void) {
    unsigned char index;
    unsigned char loopIndex;

    /* Pass 1: lengthen fricatives and voiced consonants before punctuation */
    X = 0;
    loopIndex = 0;
    while (1) {
        index = sam_phonemeindex[X];
        if (index == 255) break;
        if ((flags2[index] & 1) == 0) { X++; continue; }
        loopIndex = X;
pos48644:
        if (X == 0) break;
        X--;
        index = sam_phonemeindex[X];
        if (index != 255)
            if ((flags[index] & 128) == 0) goto pos48644;
        do {
            index = sam_phonemeindex[X];
            if (index != 255)
                if (((flags2[index] & 32) == 0) || ((flags[index] & 4) != 0)) {
                    A = sam_phonemeLength[X];
                    sam_phonemeLength[X] = (A >> 1) + A + 1; /* × 1.5 + 1 */
                }
            X++;
        } while (X != loopIndex);
        X++;
    }

    /* Pass 2: shorten vowels in various consonant contexts */
    loopIndex = 0;
    while (1) {
        X     = loopIndex;
        index = sam_phonemeindex[X];
        if (index == 255) return;

        A = flags[index] & 128;
        if (A != 0) {                  /* current phoneme is a vowel */
            X++;
            index = sam_phonemeindex[X];
            mem56 = (index == 255) ? (unsigned char)65 : flags[index];

            if ((flags[index] & 64) == 0) {
                /* Not a consonant after vowel */
                if ((index == 18) || (index == 19)) { /* RX, LX */
                    X++;
                    index = sam_phonemeindex[X];
                    if ((flags[index] & 64) != 0)
                        sam_phonemeLength[loopIndex]--; /* <V> RX|LX <C>: shorten vowel */
                    loopIndex++; continue;
                }
                loopIndex++; continue;
            }

            if ((mem56 & 4) == 0) {    /* unvoiced consonant */
                if ((mem56 & 1) == 0) { loopIndex++; continue; }
                /* <V> <unvoiced plosive>: shorten vowel by 1/8 */
                X--;
                mem56 = sam_phonemeLength[X] >> 3;
                sam_phonemeLength[X] -= mem56;
                loopIndex++; continue;
            }
            /* <V> <voiced consonant>: lengthen vowel by 5/4 + 1 */
            A = sam_phonemeLength[X - 1];
            sam_phonemeLength[X - 1] = (A >> 2) + A + 1;
            loopIndex++; continue;
        }

        /* <nasal> <stop consonant>: set nasal=5, stop=6 */
        if ((flags2[index] & 8) != 0) {
            X++;
            index = sam_phonemeindex[X];
            A = (index == 255) ? (unsigned char)(65 & 2) : (unsigned char)(flags[index] & 2);
            if (A != 0) {
                sam_phonemeLength[X]     = 6;
                sam_phonemeLength[X - 1] = 5;
            }
            loopIndex++; continue;
        }

        /* <voiced stop> {silence*} <stop consonant>: shorten both to 1/2+1 */
        if ((flags[index] & 2) != 0) {
            do { X++; index = sam_phonemeindex[X]; } while (index == 0);
            if (index == 255) {
                if ((65 & 2) == 0) { loopIndex++; continue; }
            } else if ((flags[index] & 2) == 0) {
                loopIndex++; continue;
            }
            sam_phonemeLength[X]         = (sam_phonemeLength[X] >> 1) + 1;
            X = loopIndex;
            sam_phonemeLength[loopIndex] = (sam_phonemeLength[loopIndex] >> 1) + 1;
            loopIndex++; continue;
        }

        /* <liquid consonant> <diphthong>: decrease diphthong by 2 */
        if ((flags2[index] & 16) != 0) {
            index = sam_phonemeindex[X - 1];
            if ((flags[index] & 2) != 0)
                sam_phonemeLength[X] -= 2;
        }
        loopIndex++;
    }
}
