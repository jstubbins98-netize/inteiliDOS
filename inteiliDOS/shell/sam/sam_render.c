/*
 * inteiliDOS -- shell/sam/sam_render.c
 * SAM formant synthesiser — bare-metal adaptation of render.c.
 *
 * Derived from SAM-master (Don't Ask Software C port of the C64 SAM).
 * Produces 8-bit PCM audio in sam_pcm_buf[] at ~22050 Hz.
 *
 * Bare-metal adaptations:
 *   - malloc()/free() removed; static sam_pcm_buf[SAM_PCM_MAX] used instead.
 *   - printf()/debug blocks removed entirely.
 *   - abs() replaced with inline sam_abs().
 *   - All internal functions made static.
 *   - Dead code at end of Render() removed.
 *   - sam_render_reset() added for use by Init() in sam_phoneme.c.
 */

#include "sam_render.h"
#include "RenderTabs.h"
#include <stdint.h>

/* ── PCM output buffer ─────────────────────────────────────────────────── */
#define SAM_PCM_MAX 131072           /* bytes — ~6 sec at 22050 Hz */
unsigned char sam_pcm_buf[SAM_PCM_MAX];
int           sam_pcm_len = 0;

static int           s_bufferpos        = 0;
static unsigned int  oldtimetableindex  = 0;

/* ── Shared globals (defined in sam_phoneme.c) ─────────────────────────── */
extern unsigned char A, X, Y;
extern unsigned char mem39, mem44, mem47, mem49;
extern unsigned char mem50, mem51, mem53, mem56;
extern unsigned char speed, pitch;
extern int           singmode;
extern unsigned char phonemeIndexOutput[60];
extern unsigned char stressOutput[60];
extern unsigned char phonemeLengthOutput[60];

/* ── C64 sample-timing timetable (copied verbatim from SAM) ────────────── */
static int timetable[5][5] = {
    {162, 167, 167, 127, 128},
    {226,  60,  60,   0,   0},
    {225,  60,  59,   0,   0},
    {200,   0,   0,  54,  55},
    {199,   0,   0,  54,  54}
};

/* ── Per-frame parameter arrays (256 frames max) ──────────────────────── */
static unsigned char pitches[256];
static unsigned char frequency1[256];
static unsigned char frequency2[256];
static unsigned char frequency3[256];
static unsigned char amplitude1[256];
static unsigned char amplitude2[256];
static unsigned char amplitude3[256];
static unsigned char sampledConsonantFlag[256];

/* ── Public: reset PCM cursor before a new utterance ──────────────────── */
void sam_render_reset(void) {
    s_bufferpos       = 0;
    oldtimetableindex = 0;
    sam_pcm_len       = 0;
}

/* ── Internal helpers ──────────────────────────────────────────────────── */
static inline int sam_abs(int x) { return x < 0 ? -x : x; }

static void Output8BitAry(int index, unsigned char ary[5]) {
    int k;
    s_bufferpos += timetable[oldtimetableindex][index];
    oldtimetableindex = (unsigned int)index;
    for (k = 0; k < 5; k++) {
        int pos = s_bufferpos / 50 + k;
        if (pos >= 0 && pos < SAM_PCM_MAX)
            sam_pcm_buf[pos] = ary[k];
    }
}

static void Output8Bit(int index, unsigned char val) {
    unsigned char ary[5] = {val, val, val, val, val};
    Output8BitAry(index, ary);
}

/* Indexed read/write into per-frame parameter arrays.
 * Table indices: 168=pitches, 169-171=freq1-3, 172-174=ampl1-3 */
static unsigned char Read(unsigned char p, unsigned char idx) {
    switch (p) {
    case 168: return pitches[idx];
    case 169: return frequency1[idx];
    case 170: return frequency2[idx];
    case 171: return frequency3[idx];
    case 172: return amplitude1[idx];
    case 173: return amplitude2[idx];
    case 174: return amplitude3[idx];
    default:  return 0;
    }
}

static void Write(unsigned char p, unsigned char idx, unsigned char value) {
    switch (p) {
    case 168: pitches[idx]    = value; return;
    case 169: frequency1[idx] = value; return;
    case 170: frequency2[idx] = value; return;
    case 171: frequency3[idx] = value; return;
    case 172: amplitude1[idx] = value; return;
    case 173: amplitude2[idx] = value; return;
    case 174: amplitude3[idx] = value; return;
    default: return;
    }
}

/* ── trans(): fixed-point multiply, returns (a * b) >> 8 ─────────────── */
static unsigned char trans(unsigned char mem39212, unsigned char mem39213) {
    unsigned char carry;
    int temp;
    unsigned char mem39215 = 0;
    unsigned char mem39214 = 0;
    A = 0; X = 8;
    do {
        carry      = mem39212 & 1;
        mem39212 >>= 1;
        if (carry) {
            carry = 0;
            A     = mem39215;
            temp  = (int)A + (int)mem39213;
            A     = A + mem39213;
            if (temp > 255) carry = 1;
            mem39215 = A;
        }
        temp     = mem39215 & 1;
        mem39215 = (mem39215 >> 1) | (carry ? 128 : 0);
        carry    = (unsigned char)temp;
        X--;
    } while (X != 0);
    temp     = mem39214 & 128;
    mem39214 = (mem39214 << 1) | (carry ? 1 : 0);
    carry    = (unsigned char)temp;
    temp     = mem39215 & 128;
    mem39215 = (mem39215 << 1) | (carry ? 1 : 0);
    return mem39215;
}

/* ── SetMouthThroat(): recalculate formant filter coefficients ─────────── */
void SetMouthThroat(unsigned char mouth, unsigned char throat) {
    unsigned char initialFrequency, newFrequency = 0;
    unsigned char pos;

    /* Formant 1 (mouth) for phonemes 5..29 */
    static const unsigned char mouthFormants5_29[30] = {
        0, 0, 0, 0, 0, 10, 14, 19, 24, 27, 23, 21, 16, 20, 14,
        18, 14, 18, 18, 16, 13, 15, 11, 18, 14, 11, 9, 6, 6, 6
    };
    /* Formant 2 (throat) for phonemes 5..29 */
    static const unsigned char throatFormants5_29[30] = {
        255, 255, 255, 255, 255, 84, 73, 67, 63, 40, 44, 31, 37,
        45, 73, 49, 36, 30, 51, 37, 29, 69, 24, 50, 30, 24, 83, 46, 54, 86
    };
    /* Formant 1 (mouth) for phonemes 48..53 */
    static const unsigned char mouthFormants48_53[6]  = {19, 27, 21, 27, 18, 13};
    /* Formant 2 (throat) for phonemes 48..53 */
    static const unsigned char throatFormants48_53[6] = {72, 39, 31, 43, 30, 34};

    for (pos = 5; pos != 30; pos++) {
        initialFrequency = mouthFormants5_29[pos];
        if (initialFrequency != 0) newFrequency = trans(mouth, initialFrequency);
        freq1data[pos] = newFrequency;
        initialFrequency = throatFormants5_29[pos];
        if (initialFrequency != 0) newFrequency = trans(throat, initialFrequency);
        freq2data[pos] = newFrequency;
    }
    Y = 0;
    for (pos = 48; pos != 54; pos++) {
        initialFrequency = mouthFormants48_53[Y];
        newFrequency     = trans(mouth, initialFrequency);
        freq1data[pos]   = newFrequency;
        initialFrequency = throatFormants48_53[Y];
        newFrequency     = trans(throat, initialFrequency);
        freq2data[pos]   = newFrequency;
        Y++;
    }
}

/* ── AddInflection(): add a rising (mem48=1) or falling (mem48=255) pitch */
static void AddInflection(unsigned char mem48, unsigned char phase1) {
    int Atemp;
    mem49 = X;
    A     = X;
    Atemp = A;
    A     = (unsigned char)(A - 30);
    if (Atemp <= 30) A = 0;
    X = A;
    while ((A = pitches[X]) == 127) X++;
pos48398:
    A       = (unsigned char)(A + mem48);
    phase1  = A;
    pitches[X] = A;
pos48406:
    X++;
    if (X == mem49) return;
    if (pitches[X] == 255) goto pos48406;
    A = phase1;
    goto pos48398;
}

/* ── RenderSample(): output a sampled consonant segment ──────────────────
 *
 * Voiced phonemes (Z*, ZH, V*, DH) interleave with the glottal pulse.
 * Unvoiced phonemes (S, SH, F, TH, /H, /X, T, P, K…) are rendered raw.
 */
static void RenderSample(unsigned char *mem66) {
    int tempA;
    mem49 = Y;

    A     = mem39 & 7;
    X     = A - 1;
    mem56 = X;
    mem53 = tab48426[X];
    mem47 = X;

    A = mem39 & 248;
    if (A == 0) {
        /* Voiced sample (ZH, Z*, V*, DH) */
        unsigned char phase1;
        Y      = mem49;
        A      = pitches[mem49] >> 4;
        phase1 = A ^ 255;
        Y      = *mem66;
        do {
            mem56 = 8;
            A     = sampleTable[(int)mem47 * 256 + Y];
            do {
                tempA = A;
                A     = A << 1;
                if ((tempA & 128) != 0) {
                    X = 26;
                    Output8Bit(3, (unsigned char)((X & 0xf) * 16));
                } else {
                    X = 6;
                    Output8Bit(4, (unsigned char)((X & 0xf) * 16));
                }
                mem56--;
            } while (mem56 != 0);
            Y++;
            phase1++;
        } while (phase1 != 0);
        A      = 1;
        mem44  = 1;
        *mem66 = Y;
        Y      = mem49;
        return;
    }

    /* Unvoiced sample */
    Y = A ^ 255;
pos48274:
    mem56 = 8;
    A     = sampleTable[(int)mem47 * 256 + Y];
pos48280:
    tempA = A;
    A     = A << 1;
    if ((tempA & 128) == 0) {
        X = mem53;
        Output8Bit(1, (unsigned char)((X & 0x0f) * 16));
        if (X != 0) goto pos48296;
    }
    Output8Bit(2, (unsigned char)(5 * 16));
pos48296:
    X = 0;
    mem56--;
    if (mem56 != 0) goto pos48280;
    Y++;
    if (Y != 0) goto pos48274;
    mem44 = 1;
    Y     = mem49;
}

/* ── Render(): full formant synthesis for one breath group ───────────────
 *
 * Reads phonemeIndexOutput[], stressOutput[], phonemeLengthOutput[].
 * Appends 8-bit PCM samples to sam_pcm_buf[] via Output8BitAry().
 * Updates sam_pcm_len to reflect the new end of valid data.
 */
void Render(void) {
    unsigned char phase1 = 0, phase2 = 0, phase3 = 0;
    unsigned char mem66 = 0, mem38 = 0, mem40 = 0;
    unsigned char speedcounter = 0, mem48 = 0;
    int i;

    if (phonemeIndexOutput[0] == 255) return;

    A = 0; X = 0; mem44 = 0;

    /* ── STEP 1: expand phonemes into per-frame parameter arrays ────────── */
    do {
        Y      = mem44;
        A      = phonemeIndexOutput[mem44];
        mem56  = A;
        if (A == 255) break;

        if (A == 1) { mem48 = 1;   AddInflection(mem48, phase1); }
        if (A == 2) { mem48 = 255; AddInflection(mem48, phase1); }

        phase1 = tab47492[stressOutput[Y] + 1];
        phase2 = phonemeLengthOutput[Y];
        Y      = mem56;
        do {
            frequency1[X]          = freq1data[Y];
            frequency2[X]          = freq2data[Y];
            frequency3[X]          = freq3data[Y];
            amplitude1[X]          = ampl1data[Y];
            amplitude2[X]          = ampl2data[Y];
            amplitude3[X]          = ampl3data[Y];
            sampledConsonantFlag[X]= sampledConsonantFlags[Y];
            pitches[X]             = (unsigned char)((int)pitch + (int)phase1);
            X++;
            phase2--;
        } while (phase2 != 0);
        mem44++;
    } while (mem44 != 0);

    /* ── STEP 2: linear interpolation of transitions ─────────────────────── */
    A = 0; mem44 = 0; mem49 = 0; X = 0;
    while (1) {
        Y = phonemeIndexOutput[X];
        A = phonemeIndexOutput[X + 1];
        X++;
        if (A == 255) break;

        X     = A;
        mem56 = blendRank[A];
        A     = blendRank[Y];
        if (A == mem56) {
            phase1 = outBlendLength[Y];
            phase2 = outBlendLength[X];
        } else if (A < mem56) {
            phase1 = inBlendLength[X];
            phase2 = outBlendLength[X];
        } else {
            phase1 = outBlendLength[Y];
            phase2 = inBlendLength[Y];
        }

        Y            = mem44;
        A            = mem49 + phonemeLengthOutput[mem44];
        mem49        = A;
        A            = A + phase2;
        speedcounter = A;
        mem47        = 168;
        phase3       = mem49 - phase1;
        A            = phase1 + phase2;
        mem38        = A;

        X = A;
        X -= 2;
        if ((X & 128) == 0) {
            do {
                mem40 = mem38;
                if (mem47 == 168) {
                    /* Pitch: interpolate from centre to centre */
                    unsigned char mem36, mem37;
                    mem36  = phonemeLengthOutput[mem44] >> 1;
                    mem37  = phonemeLengthOutput[mem44 + 1] >> 1;
                    mem40  = mem36 + mem37;
                    mem37  = mem37 + mem49;
                    mem36  = mem49 - mem36;
                    A      = Read(mem47, mem37);
                    Y      = mem36;
                    mem53  = (unsigned char)((int)A - (int)Read(mem47, mem36));
                } else {
                    A      = Read(mem47, speedcounter);
                    Y      = phase3;
                    mem53  = (unsigned char)((int)A - (int)Read(mem47, phase3));
                }

                /* Code47503: divide mem53 by mem40 to get per-frame delta */
                {
                    signed char m53 = (signed char)mem53;
                    unsigned char m53abs;
                    mem50   = mem53 & 128;
                    m53abs  = (unsigned char)sam_abs((int)m53);
                    if (mem40 != 0) {
                        mem51 = m53abs % mem40;
                        mem53 = (unsigned char)((signed char)m53 / (signed char)mem40);
                    } else {
                        mem51 = 0;
                        mem53 = 0;
                    }
                }

                X     = mem40;
                Y     = phase3;
                mem56 = 0;
                while (1) {
                    A     = (unsigned char)((int)Read(mem47, Y) + (int)(signed char)mem53);
                    mem48 = A;
                    Y++;
                    X--;
                    if (X == 0) break;
                    mem56 += mem51;
                    if (mem56 >= mem40) {
                        mem56 -= mem40;
                        if ((mem50 & 128) == 0) {
                            if (mem48 != 0) mem48++;
                        } else {
                            mem48--;
                        }
                    }
                    Write(mem47, Y, mem48);
                }
                mem47++;
            } while (mem47 != 175);
        }
        mem44++;
        X = mem44;
    }
    mem48 = mem49 + phonemeLengthOutput[mem44];

    /* ── STEP 3: pitch contour — subtract half F1 from pitch ────────────── */
    if (!singmode) {
        for (i = 0; i < 256; i++)
            pitches[i] = (unsigned char)((int)pitches[i] - ((int)frequency1[i] >> 1));
    }

    phase1 = 0; phase2 = 0; phase3 = 0; mem49 = 0;
    speedcounter = 72; /* SAM standard speed for first frame */

    /* ── STEP 4: amplitude rescaling (linear → dB) ──────────────────────── */
    for (i = 255; i >= 0; i--) {
        amplitude1[i] = amplitudeRescale[amplitude1[i]];
        amplitude2[i] = amplitudeRescale[amplitude2[i]];
        amplitude3[i] = amplitudeRescale[amplitude3[i]];
    }

    Y     = 0;
    A     = pitches[0];
    mem44 = A;
    X     = A;
    mem38 = (unsigned char)(A - (A >> 2));

    /* ── STEP 5: main synthesis loop ────────────────────────────────────── */
    while (1) {
        A     = sampledConsonantFlag[Y];
        mem39 = A;
        A     = A & 248;
        if (A != 0) {
            /* Sampled consonant: render from sampleTable */
            RenderSample(&mem66);
            Y    += 2;
            mem48 -= 2;
        } else {
            /* Voiced: mix three sinusoidal/rectangular formants */
            unsigned char ary[5];
            unsigned int p1 = (unsigned int)phase1 * 256;
            unsigned int p2 = (unsigned int)phase2 * 256;
            unsigned int p3 = (unsigned int)phase3 * 256;
            int k;
            for (k = 0; k < 5; k++) {
                signed char sp1  = sinus[0xff & (p1 >> 8)];
                signed char sp2  = sinus[0xff & (p2 >> 8)];
                signed char rp3  = (signed char)rectangle[0xff & (p3 >> 8)];
                signed int  sin1 = sp1 * (int)(amplitude1[Y] & 0x0f);
                signed int  sin2 = sp2 * (int)(amplitude2[Y] & 0x0f);
                signed int  rect = rp3 * (int)(amplitude3[Y] & 0x0f);
                signed int  mux  = sin1 + sin2 + rect;
                mux /= 32;
                mux += 128;
                if (mux < 0)   mux = 0;
                if (mux > 255) mux = 255;
                ary[k] = (unsigned char)mux;
                p1 += (unsigned int)frequency1[Y] * 256 / 4;
                p2 += (unsigned int)frequency2[Y] * 256 / 4;
                p3 += (unsigned int)frequency3[Y] * 256 / 4;
            }
            Output8BitAry(0, ary);
            speedcounter--;
            if (speedcounter != 0) goto pos48155;
            Y++;
            mem48--;
        }

        if (mem48 == 0) goto render_done;
        speedcounter = speed;

pos48155:
        mem44--;
        if (mem44 == 0) {
pos48159:
            A      = pitches[Y];
            mem44  = A;
            A      = (unsigned char)(A - (A >> 2));
            mem38  = A;
            phase1 = 0;
            phase2 = 0;
            phase3 = 0;
            continue;
        }
        mem38--;
        if ((mem38 != 0) || (mem39 == 0)) {
            phase1 += frequency1[Y];
            phase2 += frequency2[Y];
            phase3 += frequency3[Y];
            continue;
        }
        RenderSample(&mem66);
        goto pos48159;
    }

render_done:
    /* Commit PCM length — append a few trailing bytes of silence */
    {
        int end = s_bufferpos / 50 + 8;
        if (end > SAM_PCM_MAX) end = SAM_PCM_MAX;
        sam_pcm_len = end;
    }
}
