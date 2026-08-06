/*
 * inteiliDOS -- tetris/tetris.c
 *
 * Classic Tetris for VGA text mode 80x25, bare-metal x86.
 *
 * Board   : 10 wide x 20 tall.  Each board cell = 2 screen columns.
 * Layout  : Board on cols 0-21 (|  20 chars  |), side panel cols 23-79.
 * Controls:
 *   Left / Right     Move piece
 *   Up  / Z          Rotate clockwise (with simple wall-kick)
 *   Down             Soft drop (+1 pt per row)
 *   Space / Enter    Hard drop (+2 pts per row)
 *   P                Pause / resume
 *   Q / Escape       Quit
 *
 * USB gamepads (Xbox, DualShock, generic HID): the system USB HID driver
 * already calls keyboard_inject() so D-pad + face buttons arrive through
 * keyboard_poll() exactly like PS/2 keys — no extra code needed here.
 *
 * Music: Tetris A-Theme (Korobeiniki) loops in background using the PC
 * speaker.  timer_get_ticks() drives note timing so the game loop stays
 * non-blocking.  Sound effects briefly interrupt then music resumes.
 */

#include "tetris.h"
#include "../kernel/timer.h"
#include "../kernel/vga.h"
#include "../kernel/keyboard.h"
#include <stdint.h>

/* =========================================================================
 * Dimensions & layout
 * ========================================================================= */
#define BOARD_W    10
#define BOARD_H    20
#define CELL_W      2          /* screen columns per board cell              */
#define BLEFT       0          /* screen col of left border '|'               */
#define BTOP        0          /* screen row of top border                   */
#define PANEL_COL  23          /* screen col where side panel starts         */

/* Drop interval (ms) indexed by level 1-10 */
static const uint32_t DROP_INTERVAL[10] = {
    800, 700, 600, 500, 400, 300, 250, 200, 150, 100
};
#define drop_ms(lv) DROP_INTERVAL[(lv)<1?0:(lv)>10?9:(lv)-1]

/* =========================================================================
 * Piece colours (VGA background index)
 * ========================================================================= */
static const vga_color_t PCOL[7] = {
    VGA_COLOR_LIGHT_CYAN,       /* I */
    VGA_COLOR_LIGHT_BROWN,      /* O - bright yellow in most VGA palettes */
    VGA_COLOR_LIGHT_MAGENTA,    /* T */
    VGA_COLOR_LIGHT_GREEN,      /* S */
    VGA_COLOR_LIGHT_RED,        /* Z */
    VGA_COLOR_BLUE,             /* J */
    VGA_COLOR_BROWN,            /* L */
};

/* =========================================================================
 * Piece cell offsets: CELLS[piece 0-6][rotation 0-3][cell 0-3] = {dr, dc}
 * Offsets are within the 4x4 spawn bounding box (spawn board-col = 3).
 * ========================================================================= */
static const int8_t CELLS[7][4][4][2] = {
    /* 0: I */
    {
        {{1,0},{1,1},{1,2},{1,3}},  /* spawn: horizontal row 1          */
        {{0,2},{1,2},{2,2},{3,2}},  /* rot1:  vertical   col 2          */
        {{2,0},{2,1},{2,2},{2,3}},  /* rot2:  horizontal row 2          */
        {{0,1},{1,1},{2,1},{3,1}}   /* rot3:  vertical   col 1          */
    },
    /* 1: O */
    {
        {{0,1},{0,2},{1,1},{1,2}},
        {{0,1},{0,2},{1,1},{1,2}},
        {{0,1},{0,2},{1,1},{1,2}},
        {{0,1},{0,2},{1,1},{1,2}}
    },
    /* 2: T */
    {
        {{0,1},{1,0},{1,1},{1,2}},  /* T-up    (spawn) */
        {{0,1},{1,1},{1,2},{2,1}},  /* T-right        */
        {{1,0},{1,1},{1,2},{2,1}},  /* T-down         */
        {{0,1},{1,0},{1,1},{2,1}}   /* T-left         */
    },
    /* 3: S */
    {
        {{0,1},{0,2},{1,0},{1,1}},
        {{0,1},{1,1},{1,2},{2,2}},
        {{1,1},{1,2},{2,0},{2,1}},
        {{0,0},{1,0},{1,1},{2,1}}
    },
    /* 4: Z */
    {
        {{0,0},{0,1},{1,1},{1,2}},
        {{0,2},{1,1},{1,2},{2,1}},
        {{1,0},{1,1},{2,1},{2,2}},
        {{0,1},{1,0},{1,1},{2,0}}
    },
    /* 5: J */
    {
        {{0,0},{1,0},{1,1},{1,2}},
        {{0,1},{0,2},{1,1},{2,1}},
        {{1,0},{1,1},{1,2},{2,2}},
        {{0,1},{1,1},{2,0},{2,1}}
    },
    /* 6: L */
    {
        {{0,2},{1,0},{1,1},{1,2}},
        {{0,1},{1,1},{2,1},{2,2}},
        {{1,0},{1,1},{1,2},{2,0}},
        {{0,0},{0,1},{1,1},{2,1}}
    }
};

/* =========================================================================
 * Game state
 * ========================================================================= */
/* Board: 0 = empty, 1-7 = locked piece (colour index + 1) */
static uint8_t board[BOARD_H][BOARD_W];

typedef struct {
    int piece, rot, row, col;   /* active piece */
    int next;                   /* next piece type */
    int score, level, lines;
    int paused;
    int quit;
    int over;
    int dirty;                  /* set whenever board must be redrawn */
} GS;
static GS g;

/* LCG pseudo-random, seeded from timer at start */
static uint32_t rng_s;
static int rand7(void) {
    rng_s = rng_s * 1664525u + 1013904223u;
    return (int)((rng_s >> 16) & 0x7FFFu) % 7;
}

/* =========================================================================
 * Board / collision helpers
 * ========================================================================= */
static int collides(int p, int rot, int pr, int pc)
{
    const int8_t (*cs)[2] = CELLS[p][rot];
    int i;
    for (i = 0; i < 4; i++) {
        int r = pr + cs[i][0];
        int c = pc + cs[i][1];
        if (r < 0 || c < 0 || c >= BOARD_W || r >= BOARD_H) return 1;
        if (board[r][c]) return 1;
    }
    return 0;
}

static void lock_cur(void)
{
    const int8_t (*cs)[2] = CELLS[g.piece][g.rot];
    uint8_t v = (uint8_t)(g.piece + 1);
    int i;
    for (i = 0; i < 4; i++) {
        int r = g.row + cs[i][0];
        int c = g.col + cs[i][1];
        if (r >= 0 && r < BOARD_H && c >= 0 && c < BOARD_W)
            board[r][c] = v;
    }
}

static int clear_full(void)
{
    int n = 0, r;
    for (r = BOARD_H - 1; r >= 0; ) {
        int full = 1, c;
        for (c = 0; c < BOARD_W; c++) if (!board[r][c]) { full = 0; break; }
        if (full) {
            int rr;
            for (rr = r; rr > 0; rr--)
                for (c = 0; c < BOARD_W; c++)
                    board[rr][c] = board[rr-1][c];
            for (c = 0; c < BOARD_W; c++) board[0][c] = 0;
            n++;
        } else {
            r--;
        }
    }
    return n;
}

static int ghost_row(void)
{
    int gr = g.row;
    while (!collides(g.piece, g.rot, gr + 1, g.col)) gr++;
    return gr;
}

/* Spawn next piece; returns 0 if blocked (game over) */
static int spawn_next(void)
{
    g.piece = g.next;
    g.rot   = 0;
    g.row   = 0;
    g.col   = 3;
    g.next  = rand7();
    return !collides(g.piece, g.rot, g.row, g.col);
}

static void add_score(int lines, int bonus)
{
    static const int pts[5] = {0, 100, 300, 500, 800};
    if (lines >= 1 && lines <= 4) g.score += pts[lines] * g.level;
    g.score += bonus;
    g.lines += lines;
    int nl = g.lines / 10 + 1;
    if (nl > 10) nl = 10;
    if (nl > g.level) g.level = nl;
}

/* =========================================================================
 * Sound effects
 * (Each SFX briefly takes over the PC speaker; resets mus_end so music
 *  picks up at the next note boundary immediately afterwards.)
 * ========================================================================= */
static uint32_t mus_end = 0;
static int      mus_idx = 0;

static void sfx_place(void)
{
    speaker_on(196); timer_sleep(20); speaker_off(); timer_sleep(5);
    mus_end = 0;
}

static void sfx_clear(int n)
{
    if (n >= 4) {
        speaker_on(1047); timer_sleep(50);
        speaker_on(1319); timer_sleep(50);
        speaker_on(1760); timer_sleep(80);
    } else {
        speaker_on(659);  timer_sleep(30);
        speaker_on(880);  timer_sleep(40);
    }
    speaker_off();
    mus_end = 0;
}

static void sfx_levelup(void)
{
    speaker_on(523);  timer_sleep(30);
    speaker_on(784);  timer_sleep(30);
    speaker_on(1047); timer_sleep(60);
    speaker_off();
    mus_end = 0;
}

static void sfx_gameover(void)
{
    int freqs[] = {880, 784, 698, 622, 554, 494, 440, 0};
    int i;
    for (i = 0; freqs[i]; i++) {
        speaker_on(freqs[i]); timer_sleep(70);
        speaker_off();        timer_sleep(10);
    }
    mus_end = 0;
}

/* =========================================================================
 * Music — Tetris A-Theme (Korobeiniki), 93.7-second loop
 * ========================================================================= */
typedef struct { unsigned int freq; int ms; } MNote;

static const MNote MUSIC[] = {
{ 1319,  510}, {  988,  246}, { 1047,  249}, { 1175,  249},
    { 1319,  113}, {  247,    8}, { 1175,  117}, {  247,    7},
    { 1047,  246}, {  988,  247}, {    0,    2}, {  880,  759},
    { 1047,  249}, { 1319,  495}, { 1175,  246}, { 1047,  247},
    {    0,    2}, {  988,  759}, { 1047,  249}, { 1175,  495},
    { 1319,  493}, {    0,    2}, { 1047,  265}, {  523,  248},
    {  880,  246}, {  659,  249}, {  880,  613}, {    0,    8},
    {  988,  113}, {    0,    8}, { 1047,  113}, {    0,    8},
    { 1175,  119}, {    0,   10}, { 1319,  265}, { 1175,  494},
    { 1397,  249}, { 1760,  495}, { 1568,  246}, { 1397,  247},
    {    0,    2}, { 1319,  759}, { 1047,  249}, { 1319,  495},
    { 1175,  246}, { 1047,  247}, {    0,    2}, {  988,  759},
    { 1047,  249}, { 1175,  495}, { 1319,  493}, {    0,    2},
    { 1047,  513}, {  880,  866}, {  165,   97}, {  147,   93},
    {  131,   94}, {  123,   98}, {    0,    2}, { 2637,  388},
    { 1976,  185}, { 2093,  186}, { 2349,  186}, { 2637,   85},
    {  247,    6}, { 2349,   88}, {  247,    5}, { 2093,  185},
    { 1976,  185}, {    0,    2}, { 1760,  569}, { 2093,  186},
    { 2637,  371}, { 2349,  185}, { 2093,  185}, {    0,    2},
    { 1976,  569}, { 2093,  186}, { 2349,  371}, { 2637,  370},
    {    0,    2}, { 2093,  384}, { 1760,  830}, {    0,    6},
    { 1976,   85}, {    0,    6}, { 2093,   85}, {    0,    6},
    { 2349,   89}, {    0,    7}, { 2637,  198}, { 2349,  371},
    { 2794,  186}, { 3520,  371}, { 3136,  185}, { 2794,  185},
    {    0,    2}, { 2637,  569}, { 2093,  186}, { 2637,  371},
    { 2349,  185}, { 2093,  185}, {    0,    2}, { 1976,  569},
    { 2093,  186}, { 2349,  371}, { 2637,  370}, {    0,    2},
    { 2093,  384}, { 1760,  742}, {   55,  370}, {    0,    2},
    { 1319,  112}, {  880,   86}, { 1760,   96}, {  880,   90},
    { 1047,   99}, {  880,   85}, { 1760,   96}, {  880,   90},
    { 1319,  101}, {  880,   85}, { 1760,   96}, {  880,   89},
    { 1047,   99}, {  880,   86}, { 1760,   96}, {  880,   91},
    { 1319,  113}, {  831,   86}, { 1661,   96}, {  831,   90},
    { 1175,   99}, {  831,   85}, { 1661,   96}, {  831,   90},
    { 1319,  101}, {  831,   85}, { 1661,   96}, {  831,   89},
    {  988,   99}, {  831,   86}, { 1661,   96}, {  831,   91},
    { 1319,  113}, {  880,   86}, { 1760,   96}, {  880,   90},
    { 1047,   99}, {  880,   85}, { 1760,   96}, {  880,   90},
    { 1319,  101}, {  880,   85}, { 1760,   96}, {  880,   89},
    { 1047,   99}, {  880,   86}, { 1760,   96}, {  880,   91},
    { 1319,  113}, {  831,   86}, { 1661,   96}, {  831,   90},
    { 1175,   99}, {  831,   85}, { 1661,   96}, {  831,   90},
    { 1319,  101}, {  831,   85}, { 1661,  184}, { 3322,   99},
    { 1661,  181}, {  831,   91}, { 1319,  113}, {  880,   86},
    { 1760,   96}, {  880,   90}, { 1047,   99}, {  880,   85},
    { 1760,   96}, {  880,   90}, { 1319,  101}, {  880,   85},
    { 1760,   96}, {  880,   89}, { 1047,   99}, {  880,   86},
    { 1760,   96}, {  880,   91}, { 1319,  113}, {  831,   86},
    { 1661,   96}, {  831,   90}, { 1175,   99}, {  831,   85},
    { 1661,   96}, {  831,   90}, { 1319,  101}, {  831,   85},
    { 1661,   96}, {  831,   89}, {  988,   99}, {  831,   86},
    { 1661,   96}, {  831,   91}, { 1319,  113}, {  880,   86},
    { 1760,   96}, {  880,   89}, { 1047,   99}, {  880,   85},
    { 1760,   96}, {  880,   90}, { 1319,  101}, {  880,   85},
    { 1760,   96}, {  880,   89}, { 1047,   99}, {  880,   86},
    { 1760,   96}, {  880,   91}, { 1319,  113}, {  831,   86},
    { 1661,   96}, {  831,   90}, {  988,   99}, {  831,   85},
    { 1661,   96}, {  831,   90}, { 1319,  101}, {  831,   85},
    { 1661,  179}, { 3520,   37}, { 3136,   37}, { 2794,   37},
    { 2637,   37}, { 2349,   37}, { 2093,   37}, { 1976,   37},
    { 1760,   37}, { 1568,   37}, { 1397,   37}, {  104,    1},
    {  659,    1}, { 1319,  384}, {  988,  185}, { 1047,  186},
    { 1175,  186}, { 1319,   85}, {  247,    6}, { 1175,   88},
    {  247,    5}, { 1047,  185}, {  988,  185}, {    0,    2},
    {  880,  569}, { 1047,  186}, { 1319,  371}, { 1175,  185},
    { 1047,  185}, {    0,    2}, {  988,  569}, { 1047,  186},
    { 1175,  371}, { 1319,  370}, {    0,    2}, { 1047,  198},
    {  523,  186}, {  880,  185}, {  659,  186}, {  880,  459},
    {    0,    6}, {  988,   85}, {    0,    6}, { 1047,   85},
    {    0,    6}, { 1175,   89}, {    0,    7}, { 1319,  198},
    { 1175,  371}, { 1397,  186}, { 1760,  371}, { 1568,  185},
    { 1397,  185}, {    0,    2}, { 1319,  569}, { 1047,  186},
    { 1319,  371}, { 1175,  185}, { 1047,  185}, {    0,    2},
    {  988,  569}, { 1047,  186}, { 1175,  371}, { 1319,  370},
    {    0,    2}, { 1047,  384}, {  880,  745}, {  165,   97},
    {  147,   93}, {  131,   94}, {  123,   98}, {    0,    2},
    { 2637,  388}, { 1976,  185}, { 2093,  186}, { 2349,  186},
    { 2637,   85}, {  247,    6}, { 2349,   88}, {  247,    5},
    { 2093,  185}, { 1976,  185}, {    0,    2}, { 1760,  569},
    { 2093,  186}, { 2637,  371}, { 2349,  185}, { 2093,  185},
    {    0,    2}, { 1976,  569}, { 2093,  186}, { 2349,  371},
    { 2637,  370}, {    0,    2}, { 2093,  384}, { 1760,  830},
    {    0,    6}, { 1976,   85}, {    0,    6}, { 2093,   85},
    {    0,    6}, { 2349,   89}, {    0,    7}, { 2637,  198},
    { 2349,  371}, { 2794,  186}, { 3520,  371}, { 3136,  185},
    { 2794,  185}, {    0,    2}, { 2637,  569}, { 2093,  186},
    { 2637,  371}, { 2349,  185}, { 2093,  185}, {    0,    2},
    { 1976,  569}, { 2093,  186}, { 2349,  371}, { 2637,  370},
    {    0,    2}, { 2093,  384}, { 1760,  592}, {  880,    5},
    { 2637,   47}, { 2794,   47}, { 3136,   47}, {  880,    4},
    { 3520,  370}, {    0,    2}, {  880,  107}, { 1047,   91},
    { 1319,   91}, { 1760,   94}, { 2093,   99}, { 1760,   90},
    { 1319,   91}, { 1047,   95}, {  880,   91}, { 1047,   90},
    { 1319,   91}, { 1760,   94}, { 2093,   99}, { 1760,   91},
    { 1319,   91}, { 1047,   95}, {  831,  104}, {  988,   91},
    { 1175,   91}, { 1319,   94}, { 1661,   94}, { 1976,   95},
    { 1661,   91}, { 1319,   95}, {  988,   96}, {  831,   86},
    {  988,   92}, { 1319,   95}, { 1661,   96}, { 1976,   98},
    { 1661,   93}, { 1319,   99}, {  880,  107}, { 1047,   91},
    { 1319,   91}, { 1760,   94}, { 2093,   99}, { 1760,   90},
    { 1319,   91}, { 1047,   95}, {  880,   91}, { 1047,   90},
    { 1319,   91}, { 1760,   94}, { 2093,   99}, { 1760,   91},
    { 1319,   91}, { 1047,   95}, {  831,  109}, {  415,   86},
    {  831,  185}, { 1661,   99}, {  831,   85}, { 1319,   96},
    {  659,  191}, {  330,   86}, {  659,  187}, { 1319,  189},
    { 2637,   98}, { 1319,   99}, {  880,  112}, {  440,   86},
    {  880,  185}, { 1760,   99}, {  880,   85}, { 1760,  186},
    { 3520,  101}, { 1760,  181}, {  880,  188}, {  440,   86},
    {  880,   96}, {  440,   91}, {  659,  113}, {  330,   86},
    {  659,  185}, { 1319,   99}, {  659,   85}, { 1319,  186},
    { 1976,  101}, {  988,  183}, {  494,   90}, {  988,   96},
    { 1319,   98}, {  659,   88}, { 1319,  100}, { 1760,  111},
    { 1661,   91}, { 1760,   91}, { 1661,   94}, { 1760,   94},
    { 1661,   90}, { 1760,   90}, { 1661,   95}, { 1760,   96},
    { 1661,   90}, { 1760,   90}, { 1661,   94}, { 1760,   94},
    { 1661,   90}, { 1760,   90}, { 1661,   94}, { 1319,    2},
    { 1760,  113}, { 1319,   91}, { 1047,   91}, {  880,   89},
    { 1047,   99}, {  880,   85}, { 1047,   91}, { 1319,  100},
    { 1047,   96}, {  880,   90}, {  659,   92}, {  523,   91},
    {  659,  101}, {  523,   93}, {  440,   88}, {  523,   99},
    { 1319,  387}, {  988,  184}, { 1047,  186}, { 1175,  185},
    { 1319,   90}, { 1175,   93}, { 1047,  183}, {  988,  184},
    {    0,    2}, {  880,  197}, {  523,  184}, {  880,  184},
    { 1047,  186}, { 1319,  373}, { 1175,  188}, { 1047,  191},
    {    0,    2}, {  988,  197}, {  494,  184}, {  988,  184},
    { 1047,  186}, { 1175,  185}, {  587,  184}, { 1319,  183},
    {  659,  184}, {    0,    2}, { 1047,  197}, {  440,  184},
    {  880,  184}, {  440,  186}, {  880,  188}, {  440,  282},
    {  494,   93}, {  523,   93}, {  587,   97}, {    0,    2},
    {  659,  197}, {  587,  371}, {  698,  186}, {  880,  371},
    {  784,  183}, {  698,  184}, {    0,    2}, {  659,  382},
    {  440,  184}, {  523,  186}, {  659,  371}, {  587,  183},
    {  523,  184}, {    0,    2}, {  494,  567}, {  523,  186},
    {  587,  375}, {  659,  382}, {    0,    2}, {  523,  387},
    {  880,  836}, {  988,   90}, { 1047,   90}, { 1175,   94},
    {    0,    2}, { 1319,  198}, { 1175,  186}, {  440,  185},
    { 1397,  186}, { 1760,  371}, { 1568,  185}, { 1397,  185},
    {    0,    2}, { 1319,  384}, {  659,  185}, { 1047,  186},
    { 1319,  371}, { 1175,  185}, { 1047,  185}, {    0,    2},
    {  988,  569}, { 1047,  186}, { 1175,  371}, { 1319,  370},
    {    0,    2}, { 1047,  384}, { 1760,  222}, { 1047,    2},
    { 2637,   47}, { 2794,   47}, { 3136,   47}, { 1047,    7},
    { 3520,  742}
};
#define MUSIC_COUNT 569

static void music_tick(uint32_t now)
{
    if (now < mus_end) return;
    unsigned int f = MUSIC[mus_idx].freq;
    mus_end = now + (uint32_t)MUSIC[mus_idx].ms;
    if (f == 0) speaker_off(); else speaker_on(f);
    mus_idx++;
    if (mus_idx >= MUSIC_COUNT) mus_idx = 0;
}

/* =========================================================================
 * VGA rendering
 * ========================================================================= */

/* Erase a board cell (2 chars) with dark-grey space on black */
static void draw_empty(int scr_row, int bc)
{
    vga_set_cursor(scr_row, BLEFT + 1 + bc * CELL_W);
    vga_put_colored(' ', VGA_COLOR_DARK_GREY, VGA_COLOR_BLACK);
    vga_put_colored(' ', VGA_COLOR_DARK_GREY, VGA_COLOR_BLACK);
}

/* Fill a board cell with a solid colour */
static void draw_filled(int scr_row, int bc, vga_color_t col)
{
    vga_set_cursor(scr_row, BLEFT + 1 + bc * CELL_W);
    vga_put_colored(' ', col, col);
    vga_put_colored(' ', col, col);
}

/* Draw ghost cell (two ':' in dark grey) */
static void draw_ghost(int scr_row, int bc)
{
    vga_set_cursor(scr_row, BLEFT + 1 + bc * CELL_W);
    vga_put_colored(':', VGA_COLOR_DARK_GREY, VGA_COLOR_BLACK);
    vga_put_colored(':', VGA_COLOR_DARK_GREY, VGA_COLOR_BLACK);
}

/* Full board redraw (called when g.dirty is set) */
static void render_board(void)
{
    if (!g.dirty) return;
    g.dirty = 0;

    int gr = ghost_row();
    const int8_t (*cs)[2] = CELLS[g.piece][g.rot];

    /* Build current-piece and ghost masks */
    uint8_t cur[BOARD_H][BOARD_W];
    uint8_t gho[BOARD_H][BOARD_W];
    int r, c, i;
    for (r = 0; r < BOARD_H; r++)
        for (c = 0; c < BOARD_W; c++) { cur[r][c] = 0; gho[r][c] = 0; }

    for (i = 0; i < 4; i++) {
        int pr = g.row + cs[i][0], pc = g.col + cs[i][1];
        if (pr >= 0 && pr < BOARD_H && pc >= 0 && pc < BOARD_W)
            cur[pr][pc] = 1;
        int ghpr = gr + cs[i][0];
        if (ghpr >= 0 && ghpr < BOARD_H && pc >= 0 && pc < BOARD_W)
            gho[ghpr][pc] = 1;
    }

    for (r = 0; r < BOARD_H; r++) {
        int sr = BTOP + 1 + r;
        for (c = 0; c < BOARD_W; c++) {
            if (board[r][c]) {
                draw_filled(sr, c, PCOL[board[r][c] - 1]);
            } else if (cur[r][c]) {
                draw_filled(sr, c, PCOL[g.piece]);
            } else if (gho[r][c] && gr != g.row) {
                draw_ghost(sr, c);
            } else {
                draw_empty(sr, c);
            }
        }
    }
}

/* Draw the static chrome: borders, labels, controls */
static void render_frame(void)
{
    int r, c;

    vga_set_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK);

    /* Top border */
    vga_set_cursor(BTOP, BLEFT);
    vga_putchar('+');
    for (c = 0; c < BOARD_W * CELL_W; c++) vga_putchar('-');
    vga_putchar('+');

    /* Side borders */
    for (r = 0; r < BOARD_H; r++) {
        vga_set_cursor(BTOP + 1 + r, BLEFT);
        vga_putchar('|');
        vga_set_cursor(BTOP + 1 + r, BLEFT + 1 + BOARD_W * CELL_W);
        vga_putchar('|');
    }

    /* Bottom border */
    vga_set_cursor(BTOP + BOARD_H + 1, BLEFT);
    vga_putchar('+');
    for (c = 0; c < BOARD_W * CELL_W; c++) vga_putchar('-');
    vga_putchar('+');

    /* Title */
    vga_set_color(VGA_COLOR_LIGHT_CYAN, VGA_COLOR_BLACK);
    vga_set_cursor(0, PANEL_COL);
    vga_puts("  TETRIS");

    /* Static labels */
    vga_set_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK);
    vga_set_cursor(2,  PANEL_COL); vga_puts("SCORE:");
    vga_set_cursor(5,  PANEL_COL); vga_puts("LEVEL:");
    vga_set_cursor(8,  PANEL_COL); vga_puts("LINES:");
    vga_set_cursor(11, PANEL_COL); vga_puts("NEXT:");

    /* Controls hint */
    vga_set_color(VGA_COLOR_DARK_GREY, VGA_COLOR_BLACK);
    vga_set_cursor(17, PANEL_COL); vga_puts("CONTROLS:");
    vga_set_cursor(18, PANEL_COL); vga_puts("<- ->  Move");
    vga_set_cursor(19, PANEL_COL); vga_puts("Up     Rotate");
    vga_set_cursor(20, PANEL_COL); vga_puts("Down   Soft drop");
    vga_set_cursor(21, PANEL_COL); vga_puts("Space  Hard drop");
    vga_set_cursor(22, PANEL_COL); vga_puts("P      Pause");
    vga_set_cursor(23, PANEL_COL); vga_puts("Q/Esc  Quit");
}

/* Refresh score / level / lines numbers on side panel */
static void render_stats(void)
{
    char buf[12];
    int v, i;

    vga_set_color(VGA_COLOR_WHITE, VGA_COLOR_BLACK);

    /* Score */
    vga_set_cursor(3, PANEL_COL);
    v = g.score; i = 10; buf[10] = 0;
    do { buf[--i] = (char)('0' + v % 10); v /= 10; } while (v && i > 0);
    vga_puts(buf + i);
    vga_puts("          "); /* clear old trailing digits */
    vga_set_cursor(3, PANEL_COL);
    vga_puts(buf + i);

    /* Level */
    vga_set_cursor(6, PANEL_COL);
    v = g.level;
    buf[0] = (char)('0' + v / 10);
    buf[1] = (char)('0' + v % 10);
    buf[2] = 0;
    vga_puts(buf);

    /* Lines */
    vga_set_cursor(9, PANEL_COL);
    v = g.lines; i = 8; buf[8] = 0;
    do { buf[--i] = (char)('0' + v % 10); v /= 10; } while (v && i > 0);
    if (i == 8) { buf[7] = '0'; i = 7; }
    vga_puts(buf + i);
    vga_puts("     ");
    vga_set_cursor(9, PANEL_COL);
    vga_puts(buf + i);

    /* Pause indicator row 24 */
    vga_set_cursor(24, PANEL_COL);
    if (g.paused) {
        vga_set_color(VGA_COLOR_LIGHT_RED, VGA_COLOR_BLACK);
        vga_puts("** PAUSED — press P **");
        vga_set_color(VGA_COLOR_WHITE, VGA_COLOR_BLACK);
    } else {
        vga_puts("                      ");
    }
}

/* Redraw the next-piece preview */
static void render_next(void)
{
    int r, i;
    /* Clear 4 rows x 8 cols */
    for (r = 0; r < 4; r++) {
        vga_set_cursor(12 + r, PANEL_COL);
        vga_set_color(VGA_COLOR_BLACK, VGA_COLOR_BLACK);
        vga_puts("        ");
    }
    /* Draw next piece cells */
    const int8_t (*cs)[2] = CELLS[g.next][0];
    vga_color_t col = PCOL[g.next];
    for (i = 0; i < 4; i++) {
        int pr = cs[i][0], pc = cs[i][1];
        vga_set_cursor(12 + pr, PANEL_COL + pc * 2);
        vga_put_colored(' ', col, col);
        vga_put_colored(' ', col, col);
    }
}

/* =========================================================================
 * Input
 * ========================================================================= */
static void do_rotate(void)
{
    int nr = (g.rot + 1) & 3;
    if (!collides(g.piece, nr, g.row, g.col))
        { g.rot = nr; g.dirty = 1; return; }
    /* Wall-kick: try +1 and -1 column */
    if (!collides(g.piece, nr, g.row, g.col + 1))
        { g.rot = nr; g.col++; g.dirty = 1; return; }
    if (!collides(g.piece, nr, g.row, g.col - 1))
        { g.rot = nr; g.col--; g.dirty = 1; return; }
    /* For I-piece, try 2-column kicks */
    if (!collides(g.piece, nr, g.row, g.col + 2))
        { g.rot = nr; g.col += 2; g.dirty = 1; return; }
    if (!collides(g.piece, nr, g.row, g.col - 2))
        { g.rot = nr; g.col -= 2; g.dirty = 1; return; }
}

static void do_hard_drop(void)
{
    int bonus = 0;
    while (!collides(g.piece, g.rot, g.row + 1, g.col)) {
        g.row++; bonus += 2;
    }
    lock_cur();
    int prev_lv = g.level;
    int n = clear_full();
    add_score(n, bonus);
    if (n > 0) { render_next(); sfx_clear(n); }
    else        { sfx_place(); }
    if (g.level > prev_lv) { sfx_levelup(); render_frame(); }
    if (!spawn_next()) { g.over = 1; }
    render_next();
    g.dirty = 1;
}

static void handle_key(int k)
{
    if (k == 'q' || k == 'Q' || k == KEY_ESCAPE) { g.quit = 1; return; }
    if (k == 'p' || k == 'P')                    { g.paused ^= 1; g.dirty = 1; return; }
    if (g.paused) return;

    switch (k) {
        case KEY_LEFT:
            if (!collides(g.piece, g.rot, g.row, g.col - 1))
                { g.col--; g.dirty = 1; }
            break;
        case KEY_RIGHT:
            if (!collides(g.piece, g.rot, g.row, g.col + 1))
                { g.col++; g.dirty = 1; }
            break;
        case KEY_UP: case 'z': case 'Z':
            do_rotate();
            break;
        case KEY_DOWN:
            if (!collides(g.piece, g.rot, g.row + 1, g.col))
                { g.row++; g.score++; g.dirty = 1; }
            break;
        case ' ': case KEY_ENTER:
            do_hard_drop();
            break;
    }
}

/* =========================================================================
 * Game-over overlay
 * ========================================================================= */
static void show_game_over(void)
{
    sfx_gameover();

    /* Dark overlay on board rows 8-12 */
    vga_set_color(VGA_COLOR_LIGHT_RED, VGA_COLOR_BLACK);
    vga_set_cursor(8,  1); vga_puts("                    ");
    vga_set_cursor(9,  1); vga_puts("    ** GAME OVER **  ");
    vga_set_cursor(10, 1); vga_puts("                    ");

    /* Print final score */
    char buf[14]; int v = g.score, i = 12; buf[12] = 0;
    do { buf[--i] = (char)('0' + v % 10); v /= 10; } while (v && i > 0);
    vga_set_cursor(11, 1); vga_puts("  Score: ");
    vga_puts(buf + i);
    vga_set_cursor(12, 1); vga_puts("  Press Q or Esc    ");

    while (1) {
        int k = keyboard_poll();
        if (k == 'q' || k == 'Q' || k == KEY_ESCAPE || k == KEY_ENTER || k == ' ')
            break;
        timer_sleep(16);
    }
}

/* =========================================================================
 * Public entry point
 * ========================================================================= */
void tetris_run(void)
{
    int r, c;

    /* Seed RNG from current tick */
    rng_s = timer_get_ticks() ^ 0xDEADBEEFu;

    /* Clear board */
    for (r = 0; r < BOARD_H; r++)
        for (c = 0; c < BOARD_W; c++)
            board[r][c] = 0;

    /* Init state */
    g.next  = rand7();
    g.score = 0; g.level = 1; g.lines = 0;
    g.paused = 0; g.quit = 0; g.over = 0; g.dirty = 1;

    /* Blank screen and draw chrome */
    vga_set_color(VGA_COLOR_BLACK, VGA_COLOR_BLACK);
    vga_clear();
    render_frame();

    spawn_next();
    render_next();
    render_stats();

    /* Kick music */
    mus_idx = 0; mus_end = 0;

    uint32_t drop_time = timer_get_ticks() + drop_ms(1);

    /* ── Main game loop ─────────────────────────────────────────────────── */
    while (!g.quit && !g.over) {
        uint32_t now = timer_get_ticks();

        /* Input (non-blocking — also catches USB-HID gamepad via inject) */
        int k = keyboard_poll();
        if (k >= 0) handle_key(k);

        /* Background music tick */
        music_tick(now);

        /* Auto-drop */
        if (!g.paused && now >= drop_time) {
            if (!collides(g.piece, g.rot, g.row + 1, g.col)) {
                g.row++;
                g.dirty = 1;
            } else {
                /* Piece landed — lock, clear, spawn */
                lock_cur();
                int prev_lv = g.level;
                int n = clear_full();
                add_score(n, 0);
                if (n > 0) { render_next(); sfx_clear(n); }
                else        { sfx_place(); }
                if (g.level > prev_lv) { sfx_levelup(); render_frame(); }
                if (!spawn_next()) { g.over = 1; }
                render_next();
                g.dirty = 1;
            }
            drop_time = now + drop_ms(g.level);
        }

        /* Render only when something changed */
        if (g.dirty) {
            render_board();
            render_stats();
        }

        /* 1 ms sleep keeps CPU from spinning and gives PIT ticks a chance */
        timer_sleep(1);
    }

    speaker_off();

    if (g.over) show_game_over();

    vga_set_color(VGA_COLOR_WHITE, VGA_COLOR_BLACK);
    vga_clear();
}
