/*
 * inteilidOS -- shell/iedit.c
 * iEdit: built-in full-screen text editor
 *
 * Layout (80x25 VGA):
 *   Row  0        Title bar  (black on cyan)
 *   Rows 1-22     Editing area (22 visible lines)
 *   Row  23       Status bar  (black on light-grey)
 *   Row  24       Message bar (white on black)
 *
 * Key bindings:
 *   Arrow keys       Move cursor
 *   Enter            Insert new line
 *   Backspace        Delete character before cursor
 *   Ctrl+K  (0x0B)  Delete current line
 *   Ctrl+S  (0x13)  "Save" (in-memory; no real FS)
 *   Ctrl+Q  (0x11)  Quit (asks confirmation if unsaved)
 */

#include "iedit.h"
#include "../kernel/vga.h"
#include "../kernel/keyboard.h"
#include "../kernel/memory.h"
#include <stdint.h>

/* ---- Screen geometry ---- */
#define SCR_COLS    80
#define SCR_ROWS    25
#define EDIT_TOP     1          /* first editing row on screen */
#define EDIT_ROWS   22          /* number of visible text rows */
#define STATUS_ROW  23
#define MSG_ROW     24

/* Line-number gutter: cols 0-3 = number, col 4 = '|', cols 5-79 = text */
#define GUTTER_W     5          /* cols consumed by line-number gutter     */
#define TEXT_COLS   (SCR_COLS - GUTTER_W)   /* = 75 usable text columns   */

/* ---- Buffer ---- */
#define MAX_LINES   200
#define MAX_COL     74          /* max chars per line (TEXT_COLS - 1)      */

static char text[MAX_LINES][MAX_COL + 1];
static int  tlen[MAX_LINES];    /* length of each line (no null)           */
static int  nlines;             /* total lines in buffer                   */

/* ---- Editor state ---- */
static int  cur_row;            /* cursor line in buffer (0-based)         */
static int  cur_col;            /* cursor column in buffer (0-based)       */
static int  view_top;           /* first visible buffer line               */
static int  dirty;              /* unsaved-changes flag                    */
static char fname[64];          /* filename (may be empty)                 */
static char msgline[SCR_COLS + 1]; /* message bar text                    */

/* ---- VGA direct access ---- */
/* Each cell: low byte = ASCII, high byte = attribute (bg<<4|fg) */
#define VGA_BASE ((volatile uint16_t *)0xB8000)

static inline uint16_t cell(char c, vga_color_t fg, vga_color_t bg) {
    return (uint16_t)(((uint8_t)bg << 12) | ((uint8_t)fg << 8) | (uint8_t)c);
}
static inline void wr(int col, int row, char c, vga_color_t fg, vga_color_t bg) {
    if ((unsigned)col < SCR_COLS && (unsigned)row < SCR_ROWS)
        VGA_BASE[row * SCR_COLS + col] = cell(c, fg, bg);
}
static void fill_row(int row, vga_color_t fg, vga_color_t bg) {
    for (int x = 0; x < SCR_COLS; x++) wr(x, row, ' ', fg, bg);
}
static void puts_at(int col, int row, const char *s, vga_color_t fg, vga_color_t bg) {
    while (*s && col < SCR_COLS) wr(col++, row, *s++, fg, bg);
}

/* Right-justify an integer in a field of `width` ending at column `end_col` */
static void putuint_rj(int end_col, int row, int val,
                        int width, vga_color_t fg, vga_color_t bg) {
    char tmp[12]; int i = 0;
    if (val == 0) { tmp[i++] = '0'; }
    else { int v = val; while (v) { tmp[i++] = (char)('0' + v % 10); v /= 10; } }
    for (int a = 0, b = i - 1; a < b; a++, b--) {
        char t = tmp[a]; tmp[a] = tmp[b]; tmp[b] = t;
    }
    tmp[i] = '\0';
    int start = end_col - i;
    /* spaces to the left */
    for (int x = end_col - width; x < start; x++) wr(x, row, ' ', fg, bg);
    for (int x = 0; x < i && start + x < SCR_COLS; x++)
        wr(start + x, row, tmp[x], fg, bg);
}

/* ---- Hardware cursor ---- */
static inline void outb_e(uint16_t port, uint8_t val) {
    __asm__ volatile ("outb %0, %1" : : "a"(val), "Nd"(port));
}
static void hw_cursor(int col, int row) {
    uint16_t pos = (uint16_t)(row * SCR_COLS + col);
    outb_e(0x3D4, 0x0F); outb_e(0x3D5, (uint8_t)(pos & 0xFF));
    outb_e(0x3D4, 0x0E); outb_e(0x3D5, (uint8_t)((pos >> 8) & 0xFF));
}

/* ---- Draw ---- */
static void draw_title(void) {
    fill_row(0, VGA_COLOR_BLACK, VGA_COLOR_LIGHT_CYAN);
    puts_at(1, 0, "iEdit", VGA_COLOR_BLACK, VGA_COLOR_LIGHT_CYAN);
    wr(7, 0, '|', VGA_COLOR_BLACK, VGA_COLOR_LIGHT_CYAN);
    /* filename (+ dirty marker) */
    const char *fn = fname[0] ? fname : "[No Name]";
    puts_at(9, 0, fn, VGA_COLOR_BLACK, VGA_COLOR_LIGHT_CYAN);
    if (dirty) {
        int col = 9 + (int)kstrlen(fn);
        puts_at(col, 0, " *", VGA_COLOR_BLACK, VGA_COLOR_LIGHT_CYAN);
    }
    /* hints on the right */
    const char *hints = "^Q:Quit  ^S:Save  ^K:DelLine";
    int hcol = SCR_COLS - (int)kstrlen(hints) - 1;
    if (hcol > 30)
        puts_at(hcol, 0, hints, VGA_COLOR_BLACK, VGA_COLOR_LIGHT_CYAN);
}

static void draw_area(void) {
    for (int sr = 0; sr < EDIT_ROWS; sr++) {
        int br = view_top + sr;
        int srow = EDIT_TOP + sr;
        fill_row(srow, VGA_COLOR_WHITE, VGA_COLOR_BLACK);

        if (br < nlines) {
            /* line number (right-justified in cols 0-3) */
            putuint_rj(3, srow, br + 1, 4,
                       VGA_COLOR_DARK_GREY, VGA_COLOR_BLACK);
            wr(4, srow, '|', VGA_COLOR_DARK_GREY, VGA_COLOR_BLACK);
            /* text */
            for (int c = 0; c < tlen[br] && c < TEXT_COLS; c++)
                wr(GUTTER_W + c, srow, text[br][c],
                   VGA_COLOR_WHITE, VGA_COLOR_BLACK);
        } else {
            /* tilde: lines past end of file */
            wr(0, srow, '~', VGA_COLOR_DARK_GREY, VGA_COLOR_BLACK);
        }
    }
}

static void draw_status(void) {
    fill_row(STATUS_ROW, VGA_COLOR_BLACK, VGA_COLOR_LIGHT_GREY);
    /* Ln:  Col: */
    puts_at(1, STATUS_ROW, "Ln:", VGA_COLOR_BLACK, VGA_COLOR_LIGHT_GREY);
    putuint_rj(7,  STATUS_ROW, cur_row + 1, 4,
               VGA_COLOR_BLACK, VGA_COLOR_LIGHT_GREY);
    puts_at(9, STATUS_ROW, "Col:", VGA_COLOR_BLACK, VGA_COLOR_LIGHT_GREY);
    putuint_rj(16, STATUS_ROW, cur_col + 1, 4,
               VGA_COLOR_BLACK, VGA_COLOR_LIGHT_GREY);
    puts_at(19, STATUS_ROW, "Lines:", VGA_COLOR_BLACK, VGA_COLOR_LIGHT_GREY);
    putuint_rj(27, STATUS_ROW, nlines, 4,
               VGA_COLOR_BLACK, VGA_COLOR_LIGHT_GREY);
    /* right: mode */
    const char *mode = dirty ? "-- MODIFIED --" : "-- inteiliDOS --";
    int mc = SCR_COLS - (int)kstrlen(mode) - 1;
    if (mc > 27) puts_at(mc, STATUS_ROW, mode,
                          VGA_COLOR_BLACK, VGA_COLOR_LIGHT_GREY);
}

static void draw_msg(void) {
    fill_row(MSG_ROW, VGA_COLOR_WHITE, VGA_COLOR_BLACK);
    if (msgline[0])
        puts_at(0, MSG_ROW, msgline, VGA_COLOR_LIGHT_GREEN, VGA_COLOR_BLACK);
}

static void redraw(void) {
    draw_title();
    draw_area();
    draw_status();
    draw_msg();
    /* place hardware cursor */
    int sc = GUTTER_W + cur_col;
    int sr = EDIT_TOP + (cur_row - view_top);
    if (sc >= SCR_COLS) sc = SCR_COLS - 1;
    hw_cursor(sc, sr);
}

static void set_msg(const char *m) {
    int i = 0;
    while (m[i] && i < SCR_COLS) { msgline[i] = m[i]; i++; }
    msgline[i] = '\0';
}

/* ---- Viewport ---- */
static void scroll_view(void) {
    if (cur_row < view_top)                 view_top = cur_row;
    if (cur_row >= view_top + EDIT_ROWS)    view_top = cur_row - EDIT_ROWS + 1;
}

static void clamp_col(void) {
    if (cur_col > tlen[cur_row]) cur_col = tlen[cur_row];
}

/* ---- Edit operations ---- */
static void op_insert_char(char c) {
    if (tlen[cur_row] >= MAX_COL) return;
    char *ln = text[cur_row];
    int   len = tlen[cur_row];
    for (int i = len; i > cur_col; i--) ln[i] = ln[i - 1];
    ln[cur_col] = c;
    ln[len + 1] = '\0';
    tlen[cur_row]++;
    cur_col++;
    dirty = 1;
}

static void op_backspace(void) {
    if (cur_col > 0) {
        char *ln  = text[cur_row];
        int   len = tlen[cur_row];
        for (int i = cur_col - 1; i < len - 1; i++) ln[i] = ln[i + 1];
        ln[len - 1] = '\0';
        tlen[cur_row]--;
        cur_col--;
        dirty = 1;
    } else if (cur_row > 0) {
        /* merge with previous line */
        int pr     = cur_row - 1;
        int pr_len = tlen[pr];
        int cr_len = tlen[cur_row];
        if (pr_len + cr_len <= MAX_COL) {
            for (int i = 0; i < cr_len; i++)
                text[pr][pr_len + i] = text[cur_row][i];
            text[pr][pr_len + cr_len] = '\0';
            tlen[pr] = pr_len + cr_len;
            /* shift lines up */
            for (int r = cur_row; r < nlines - 1; r++) {
                tlen[r] = tlen[r + 1];
                for (int c = 0; c <= tlen[r]; c++) text[r][c] = text[r + 1][c];
            }
            nlines--;
            cur_row--;
            cur_col = pr_len;
            dirty = 1;
        }
    }
}

static void op_enter(void) {
    if (nlines >= MAX_LINES) return;
    char *ln  = text[cur_row];
    int   split    = cur_col;
    int   rest_len = tlen[cur_row] - split;
    /* shift lines down to make room */
    for (int r = nlines; r > cur_row + 1; r--) {
        tlen[r] = tlen[r - 1];
        for (int c = 0; c <= tlen[r]; c++) text[r][c] = text[r - 1][c];
    }
    /* new line gets the text after the split */
    for (int c = 0; c < rest_len; c++) text[cur_row + 1][c] = ln[split + c];
    text[cur_row + 1][rest_len] = '\0';
    tlen[cur_row + 1] = rest_len;
    /* truncate current line at split */
    ln[split] = '\0';
    tlen[cur_row] = split;
    nlines++;
    cur_row++;
    cur_col = 0;
    dirty = 1;
}

static void op_delete_line(void) {
    if (nlines == 1) {
        text[0][0] = '\0'; tlen[0] = 0;
        cur_col = 0; dirty = 1;
        set_msg("Line cleared.");
        return;
    }
    for (int r = cur_row; r < nlines - 1; r++) {
        tlen[r] = tlen[r + 1];
        for (int c = 0; c <= tlen[r]; c++) text[r][c] = text[r + 1][c];
    }
    nlines--;
    if (cur_row >= nlines) cur_row = nlines - 1;
    clamp_col();
    dirty = 1;
    set_msg("Line deleted.");
}

/* ---- Entry point ---- */
void iedit_run(const char *name) {
    /* Initialise buffer */
    for (int i = 0; i < MAX_LINES; i++) { text[i][0] = '\0'; tlen[i] = 0; }
    nlines = 1;
    cur_row = cur_col = view_top = dirty = 0;
    msgline[0] = '\0';

    /* Store filename */
    if (name && name[0]) kstrncpy(fname, name, 63);
    else fname[0] = '\0';
    fname[63] = '\0';

    /* Welcome message */
    set_msg("iEdit ready.  Ctrl+Q: Quit  Ctrl+S: Save  Ctrl+K: Delete line");
    redraw();

    int quit_pending = 0;   /* set when user needs to confirm quit */

    for (;;) {
        int c = keyboard_getchar();
        msgline[0] = '\0';      /* clear message on each keystroke */

        /* ---- Ctrl combinations ---- */
        if (c == 0x11) {        /* Ctrl+Q */
            if (dirty && !quit_pending) {
                set_msg("Unsaved changes! Press Ctrl+Q again to quit without saving.");
                quit_pending = 1;
                redraw();
                continue;
            }
            break;
        }
        quit_pending = 0;

        if (c == 0x13) {        /* Ctrl+S */
            dirty = 0;
            set_msg("Saved to memory.  (Changes are lost on reboot -- no filesystem yet.)");
            redraw();
            continue;
        }

        if (c == 0x0B) {        /* Ctrl+K: delete line */
            op_delete_line();
        }

        /* ---- Navigation ---- */
        else if (c == KEY_UP) {
            if (cur_row > 0) { cur_row--; clamp_col(); }
        }
        else if (c == KEY_DOWN) {
            if (cur_row < nlines - 1) { cur_row++; clamp_col(); }
        }
        else if (c == KEY_LEFT) {
            if (cur_col > 0) {
                cur_col--;
            } else if (cur_row > 0) {
                cur_row--;
                cur_col = tlen[cur_row];
            }
        }
        else if (c == KEY_RIGHT) {
            if (cur_col < tlen[cur_row]) {
                cur_col++;
            } else if (cur_row < nlines - 1) {
                cur_row++;
                cur_col = 0;
            }
        }

        /* ---- Editing ---- */
        else if (c == KEY_BACKSPACE || c == '\b') {
            op_backspace();
        }
        else if (c == KEY_ENTER || c == '\r' || c == '\n') {
            op_enter();
        }
        else if (c == KEY_ESCAPE) {
            /* ignore */
        }
        else if (c >= 0x20 && c < 0x7F) {
            op_insert_char((char)c);
        }

        scroll_view();
        redraw();
    }

    /* Return to shell: clear screen so the shell prompt redraws cleanly */
    vga_clear();
    vga_set_color(VGA_COLOR_WHITE, VGA_COLOR_BLACK);
}
