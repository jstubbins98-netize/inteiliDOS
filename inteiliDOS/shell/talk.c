/*
 * inteilidOS -- shell/talk.c
 * InteiliTalk 1.0 — PC-speaker text-to-speech
 *
 * Synthesises speech phoneme-by-phoneme using the PC speaker.
 * The speaker API (speaker_on / speaker_off / speaker_beep) lives in
 * kernel/timer.h and is already initialised by kernel_main.
 *
 * Phoneme model
 * ─────────────
 * Each ASCII character maps to a (frequency, duration_ms) pair.
 * Common digraphs (TH, SH, CH, etc.) are detected and given a
 * distinct phoneme before falling back to single-character lookup.
 *
 * Vowels     : lower frequency, longer duration (100-120 ms)
 * Plosives   : short burst (40-60 ms)
 * Fricatives : higher frequency, medium duration (60-80 ms)
 * Nasals     : low frequency, medium duration (80-100 ms)
 * Silence    : space / punctuation — speaker off
 *
 * Screen layout during speech
 * ───────────────────────────
 *   Row 0   : title bar
 *   Rows 1-3: the text being spoken, wrapped at 78 chars
 *   Row 4   : blank
 *   Row 5   : progress bar (one block per spoken character)
 *   Row 6   : current character / phoneme info
 *   Rows 7-22: (blank)
 *   Row 23  : status
 *   Row 24  : hints
 */

#include "talk.h"
#include "../kernel/vga.h"
#include "../kernel/keyboard.h"
#include "../kernel/timer.h"
#include "../kernel/memory.h"
#include <stdint.h>
#include <stddef.h>

/* =========================================================================
 * Phoneme table
 * ========================================================================= */
typedef struct {
    uint32_t freq_hz;   /* 0 = silence */
    uint32_t dur_ms;
} Phoneme;

/* Indexed by ASCII value (0-127).  Unknown chars use freq=0, dur=50. */
static const Phoneme g_phone[128] = {
    /* 0x00-0x1F: control chars — short silence */
    ['\n'] = { 0, 200 },
    ['\t'] = { 0, 150 },

    /* Space / punctuation */
    [' ']  = { 0, 120 },
    ['.']  = { 0, 300 },
    [',']  = { 0, 200 },
    [';']  = { 0, 200 },
    [':']  = { 0, 180 },
    ['!']  = { 0, 280 },
    ['?']  = { 0, 280 },
    ['-']  = { 0, 150 },
    ['\''] = { 0,  80 },

    /* Digits — each a distinct pitch */
    ['0']  = {262, 80},
    ['1']  = {294, 80},
    ['2']  = {330, 80},
    ['3']  = {349, 80},
    ['4']  = {392, 80},
    ['5']  = {440, 80},
    ['6']  = {494, 80},
    ['7']  = {523, 80},
    ['8']  = {587, 80},
    ['9']  = {659, 80},

    /* Vowels — lower freq, longer duration */
    ['a']  = {220, 120},  /* "ah" */
    ['e']  = {330, 110},  /* "eh" */
    ['i']  = {440, 110},  /* "ih" */
    ['o']  = {275, 120},  /* "oh" */
    ['u']  = {196, 115},  /* "uh" */
    ['A']  = {220, 120},
    ['E']  = {330, 110},
    ['I']  = {440, 110},
    ['O']  = {275, 120},
    ['U']  = {196, 115},

    /* Consonants — varied by voiced/unvoiced, manner of articulation */
    ['b']  = {120,  60},  /* voiced bilabial plosive */
    ['c']  = {400,  55},
    ['d']  = {160,  65},  /* voiced alveolar plosive */
    ['f']  = {800,  70},  /* unvoiced labiodental fricative */
    ['g']  = {100,  70},  /* voiced velar plosive */
    ['h']  = {550,  45},  /* unvoiced glottal fricative */
    ['j']  = {170,  75},
    ['k']  = {380,  45},  /* unvoiced velar plosive */
    ['l']  = {260,  85},  /* lateral approximant */
    ['m']  = {110, 100},  /* bilabial nasal */
    ['n']  = {130,  85},  /* alveolar nasal */
    ['p']  = {130,  50},  /* unvoiced bilabial plosive */
    ['q']  = {420,  80},
    ['r']  = {220,  85},  /* rhotic approximant */
    ['s']  = {600,  75},  /* unvoiced alveolar sibilant */
    ['t']  = {300,  45},  /* unvoiced alveolar plosive */
    ['v']  = {500,  75},  /* voiced labiodental fricative */
    ['w']  = {196,  90},  /* bilabial approximant */
    ['x']  = {380,  80},
    ['y']  = {350,  85},  /* palatal approximant */
    ['z']  = {250,  80},  /* voiced alveolar sibilant */

    /* Upper-case consonants — same phonemes */
    ['B']  = {120,  60},
    ['C']  = {400,  55},
    ['D']  = {160,  65},
    ['F']  = {800,  70},
    ['G']  = {100,  70},
    ['H']  = {550,  45},
    ['J']  = {170,  75},
    ['K']  = {380,  45},
    ['L']  = {260,  85},
    ['M']  = {110, 100},
    ['N']  = {130,  85},
    ['P']  = {130,  50},
    ['Q']  = {420,  80},
    ['R']  = {220,  85},
    ['S']  = {600,  75},
    ['T']  = {300,  45},
    ['V']  = {500,  75},
    ['W']  = {196,  90},
    ['X']  = {380,  80},
    ['Y']  = {350,  85},
    ['Z']  = {250,  80},
};

/* Digraph overrides: two consecutive characters that map to one phoneme. */
typedef struct { char d[2]; uint32_t freq_hz; uint32_t dur_ms; } Digraph;
static const Digraph g_digraphs[] = {
    { {'t','h'}, 150, 100 },  /* "th" — interdental fricative */
    { {'T','H'}, 150, 100 },
    { {'T','h'}, 150, 100 },
    { {'t','H'}, 150, 100 },
    { {'s','h'}, 500,  85 },  /* "sh" — palato-alveolar fricative */
    { {'S','H'}, 500,  85 },
    { {'c','h'}, 350,  80 },  /* "ch" — affricate */
    { {'C','H'}, 350,  80 },
    { {'w','h'}, 200,  80 },  /* "wh" */
    { {'W','H'}, 200,  80 },
    { {'n','g'}, 115,  95 },  /* "ng" — velar nasal */
    { {'N','G'}, 115,  95 },
    { {'p','h'}, 800,  75 },  /* "ph" → 'f' */
    { {'P','H'}, 800,  75 },
    { {'o','u'}, 250, 120 },  /* "ou" → 'o' */
    { {'O','U'}, 250, 120 },
    { {'a','i'}, 220, 120 },  /* "ai" → 'a' */
    { {'A','I'}, 220, 120 },
    { {0, 0},   0,   0   },  /* sentinel */
};

/* =========================================================================
 * VGA helpers (direct memory access for speed)
 * ========================================================================= */
static volatile uint16_t *const TK_VGA = (volatile uint16_t *)0xB8000U;
#define TK_POKE(row, col, c, fg, bg) \
    (TK_VGA[(row)*80+(col)] = (uint16_t)(((bg)<<12)|((fg)<<8)|(uint8_t)(c)))

static int tk_strlen(const char *s) { int n=0; while(s[n]) n++; return n; }

static void tk_fill_row(int row, vga_color_t fg, vga_color_t bg) {
    for (int c = 0; c < 80; c++) TK_POKE(row, c, ' ', fg, bg);
}

static void tk_puts_row(int row, int col, const char *s,
                         vga_color_t fg, vga_color_t bg) {
    for (int i = 0; s[i] && col + i < 80; i++)
        TK_POKE(row, col + i, s[i], fg, bg);
}

/* =========================================================================
 * Speech synthesis
 * ========================================================================= */

/* Display the text being spoken across rows 1-3 (78 chars/line).
 * Highlights the character at index 'hi' in a different colour.
 */
static void tk_show_text(const char *text, int len, int hi) {
    for (int row = 1; row <= 3; row++) tk_fill_row(row, VGA_COLOR_WHITE, VGA_COLOR_BLACK);

    int row = 1, col = 1;
    for (int i = 0; i < len && i < 234; i++) {
        if (col >= 79) { row++; col = 1; if (row > 3) break; }
        vga_color_t fg = (i == hi) ? VGA_COLOR_BLACK : VGA_COLOR_LIGHT_GREY;
        vga_color_t bg = (i == hi) ? VGA_COLOR_LIGHT_GREEN : VGA_COLOR_BLACK;
        TK_POKE(row, col, text[i], fg, bg);
        col++;
    }
}

/* Draw a progress bar on row 5: 'done' blocks filled / 'total' wide. */
static void tk_draw_progress(int done, int total) {
    tk_fill_row(5, VGA_COLOR_BLACK, VGA_COLOR_BLACK);
    if (total <= 0) return;
    int barw = 76;
    int filled = (total > 0) ? (done * barw / total) : 0;
    if (filled > barw) filled = barw;
    for (int i = 0; i < barw; i++) {
        vga_color_t bg = (i < filled) ? VGA_COLOR_LIGHT_GREEN : VGA_COLOR_DARK_GREY;
        TK_POKE(5, 2 + i, ' ', VGA_COLOR_BLACK, bg);
    }
}

/* Show which phoneme is active on row 6. */
static void tk_show_phoneme(char c, uint32_t freq, uint32_t dur) {
    tk_fill_row(6, VGA_COLOR_BLACK, VGA_COLOR_BLACK);
    char buf[64];
    /* Manual sprintf: "  Speaking: 'X'  freq=440 Hz  dur=110 ms" */
    int n = 0;
    const char *pfx = "  Speaking: '";
    for (int i = 0; pfx[i]; i++) buf[n++] = pfx[i];
    buf[n++] = (c >= 0x20 && c < 0x7F) ? c : '?';
    buf[n++] = '\'';
    buf[n++] = ' ';
    buf[n++] = ' ';
    /* freq */
    const char *fp = "freq=";
    for (int i = 0; fp[i]; i++) buf[n++] = fp[i];
    uint32_t f = freq;
    if (f == 0) { buf[n++] = '-'; } else {
        char tmp[8]; int ti=0;
        do { tmp[ti++]=(char)('0'+f%10); f/=10; } while(f);
        for (int i=ti-1;i>=0;i--) buf[n++]=tmp[i];
    }
    const char *hz = " Hz  ";
    for (int i = 0; hz[i]; i++) buf[n++] = hz[i];
    /* dur */
    const char *dp = "dur=";
    for (int i = 0; dp[i]; i++) buf[n++] = dp[i];
    uint32_t d = dur;
    { char tmp[8]; int ti=0;
      do { tmp[ti++]=(char)('0'+d%10); d/=10; } while(d);
      for (int i=ti-1;i>=0;i--) buf[n++]=tmp[i]; }
    const char *ms = " ms";
    for (int i = 0; ms[i]; i++) buf[n++] = ms[i];
    buf[n] = '\0';
    tk_puts_row(6, 0, buf, VGA_COLOR_LIGHT_CYAN, VGA_COLOR_BLACK);
}

/* The main speech engine — speak a null-terminated string. */
void talk_speak(const char *text) {
    int len = tk_strlen(text);
    if (len <= 0) return;

    int i = 0;
    while (i < len) {
        /* Check for escape (Ctrl+C or Escape) */
        int k = keyboard_poll();
        if (k == KEY_ESCAPE || k == 0x03) { speaker_off(); break; }

        /* Check for digraph */
        uint32_t freq = 0, dur = 50;
        int advance = 1;
        if (i + 1 < len) {
            for (int d = 0; g_digraphs[d].d[0]; d++) {
                if (text[i] == g_digraphs[d].d[0] &&
                    text[i+1] == g_digraphs[d].d[1]) {
                    freq    = g_digraphs[d].freq_hz;
                    dur     = g_digraphs[d].dur_ms;
                    advance = 2;
                    break;
                }
            }
        }

        if (advance == 1) {
            /* Single-character lookup */
            unsigned idx = (unsigned char)text[i];
            if (idx < 128) {
                freq = g_phone[idx].freq_hz;
                dur  = g_phone[idx].dur_ms ? g_phone[idx].dur_ms : 50;
            }
        }

        /* Update display */
        tk_show_text(text, len, i);
        tk_draw_progress(i, len);
        tk_show_phoneme(text[i], freq, dur);

        /* Produce tone */
        if (freq > 0) {
            speaker_beep(freq, dur);
            /* Brief silence between phonemes (10 ms articulation gap) */
            speaker_off();
            timer_sleep(10);
        } else {
            speaker_off();
            timer_sleep(dur);
        }

        i += advance;
    }

    speaker_off();

    /* Show completed progress */
    tk_draw_progress(len, len);
    tk_fill_row(6, VGA_COLOR_BLACK, VGA_COLOR_BLACK);
    tk_puts_row(6, 2, "Done.", VGA_COLOR_LIGHT_GREEN, VGA_COLOR_BLACK);
}

/* =========================================================================
 * Input helper — read a line from the keyboard
 * ========================================================================= */
static int tk_readline(char *buf, int maxlen, int screen_row, int prompt_col) {
    int len = 0;
    buf[0] = '\0';
    while (1) {
        /* Redraw input line */
        for (int c = prompt_col; c < 80; c++) TK_POKE(screen_row, c, ' ', VGA_COLOR_WHITE, VGA_COLOR_BLACK);
        for (int i = 0; i < len; i++)
            TK_POKE(screen_row, prompt_col + i, buf[i], VGA_COLOR_WHITE, VGA_COLOR_BLACK);
        /* Cursor */
        TK_POKE(screen_row, prompt_col + len, '_', VGA_COLOR_LIGHT_GREEN, VGA_COLOR_BLACK);

        int k = keyboard_getchar();
        if (k == KEY_ENTER || k == '\r') { buf[len] = '\0'; return len; }
        if (k == KEY_ESCAPE) { buf[0] = '\0'; return -1; }
        if (k == KEY_BACKSPACE && len > 0) { buf[--len] = '\0'; }
        else if (k >= 0x20 && k < 0x7F && len < maxlen - 1) { buf[len++] = (char)k; buf[len] = '\0'; }
    }
}

/* =========================================================================
 * REPL — interactive InteiliTalk session
 * ========================================================================= */
void talk_run(void) {
    vga_clear();

    /* Title bar */
    tk_fill_row(0, VGA_COLOR_BLACK, VGA_COLOR_LIGHT_MAGENTA);
    tk_puts_row(0, 0, "  InteiliTalk 1.0  --  PC-Speaker Text-to-Speech",
                VGA_COLOR_BLACK, VGA_COLOR_LIGHT_MAGENTA);
    tk_puts_row(0, 60, "Ctrl+Q or EXIT to quit", VGA_COLOR_BLACK, VGA_COLOR_MAGENTA);

    /* Instructions */
    tk_puts_row(8,  2, "Type anything and press Enter to speak it.",
                VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK);
    tk_puts_row(9,  2, "Press Escape during speech to stop.",
                VGA_COLOR_DARK_GREY, VGA_COLOR_BLACK);
    tk_puts_row(10, 2, "Digraphs like TH, SH, CH, NG have distinct phonemes.",
                VGA_COLOR_DARK_GREY, VGA_COLOR_BLACK);

    /* Hint row */
    tk_fill_row(24, VGA_COLOR_DARK_GREY, VGA_COLOR_BLACK);
    tk_puts_row(24, 0,
        "  Esc=Stop speaking  Ctrl+Q=Quit  Type EXIT=Quit",
        VGA_COLOR_DARK_GREY, VGA_COLOR_BLACK);

    /* Boot chime to confirm speaker is active */
    speaker_beep(880, 80);
    speaker_off();
    timer_sleep(30);
    speaker_beep(1100, 80);
    speaker_off();

    char input[128];
    while (1) {
        /* Clear text display rows */
        for (int r = 1; r <= 7; r++) tk_fill_row(r, VGA_COLOR_BLACK, VGA_COLOR_BLACK);
        tk_fill_row(5, VGA_COLOR_BLACK, VGA_COLOR_BLACK);
        tk_fill_row(6, VGA_COLOR_BLACK, VGA_COLOR_BLACK);

        /* Status / prompt row */
        tk_fill_row(23, VGA_COLOR_BLACK, VGA_COLOR_DARK_GREY);
        tk_puts_row(23, 0, "  > ", VGA_COLOR_LIGHT_GREEN, VGA_COLOR_DARK_GREY);

        int r = tk_readline(input, 127, 23, 4);
        if (r < 0) break;   /* Escape */

        /* Check Ctrl+Q prefix or EXIT / QUIT command */
        if (input[0] == 0x11 || input[0] == 0x03) break;

        if ((input[0]=='E'||input[0]=='e') &&
            (input[1]=='X'||input[1]=='x') &&
            (input[2]=='I'||input[2]=='i') &&
            (input[3]=='T'||input[3]=='t') &&
             input[4]=='\0') break;

        if ((input[0]=='Q'||input[0]=='q') &&
            (input[1]=='U'||input[1]=='u') &&
            (input[2]=='I'||input[2]=='i') &&
            (input[3]=='T'||input[3]=='t') &&
             input[4]=='\0') break;

        if (r == 0) continue;   /* empty line */

        /* Speak! */
        tk_fill_row(23, VGA_COLOR_BLACK, VGA_COLOR_BLACK);
        tk_puts_row(23, 0, "  Speaking...", VGA_COLOR_LIGHT_CYAN, VGA_COLOR_BLACK);
        talk_speak(input);

        timer_sleep(400);
    }

    speaker_off();
    vga_clear();
    vga_set_color(VGA_COLOR_WHITE, VGA_COLOR_BLACK);
    vga_set_cursor(0, 0);
}
