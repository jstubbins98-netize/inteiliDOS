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
 *   Ctrl+Q  (0x11)  Quit (asks confirmation if unsaved)
 *   save    (typed alone on a blank line)  Save as .txt
 *   load    (typed alone on a blank line)  Open .txt
 *   quit    (typed alone on a blank line)  Return to shell
 */

#include "iedit.h"
#include "../kernel/vga.h"
#include "../kernel/keyboard.h"
#include "../kernel/memory.h"
#include "../kernel/fs.h"
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

/* Serialisation buffer for disk writes: 200 lines × 76 bytes max + NUL   */
#define IEDIT_SAVE_BUF  16384
static uint8_t iedit_save_buf[IEDIT_SAVE_BUF];

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
    const char *hints = "type 'save' or 'load' on a blank line  |  ^K:DelLine  ^Q:Quit";
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

/* ---- Save-file dialog ---- */

/* Read a short string from the user directly on the message bar.
 * Returns the number of characters entered (0 if cancelled via Escape). */
static int iedit_prompt(const char *prompt, char *buf, int maxlen) {
    int n = 0;
    buf[0] = '\0';
    for (;;) {
        /* Rebuild message: prompt + current buf + blinking-block cursor */
        char tmp[SCR_COLS + 1];
        int ti = 0;
        for (int i = 0; prompt[i] && ti < SCR_COLS - 1; i++)
            tmp[ti++] = prompt[i];
        for (int i = 0; i < n && ti < SCR_COLS - 1; i++)
            tmp[ti++] = buf[i];
        if (ti < SCR_COLS - 1) tmp[ti++] = '_';
        tmp[ti] = '\0';
        set_msg(tmp);
        draw_msg();

        int c = keyboard_getchar();
        if (c == '\r' || c == '\n') break;
        if (c == KEY_ESCAPE)        { buf[0] = '\0'; n = 0; break; }
        if ((c == '\b' || c == 0x08) && n > 0)
            buf[--n] = '\0';
        else if (c >= 0x20 && c < 0x7F && n < maxlen - 1) {
            buf[n++] = (char)c;
            buf[n]   = '\0';
        }
    }
    return n;
}

/* Append ".txt" to buf if it has no dot-extension already. */
static void iedit_ensure_txt(char *buf, int maxbuf) {
    int len = (int)kstrlen(buf);
    /* Check last 4 chars for an existing extension */
    int has_ext = (len >= 4 && buf[len - 4] == '.');
    if (!has_ext && len + 4 < maxbuf) {
        buf[len]   = '.';
        buf[len+1] = 't';
        buf[len+2] = 'x';
        buf[len+3] = 't';
        buf[len+4] = '\0';
    }
}

/* Full save-to-file dialog triggered by Ctrl+S.
 * Steps: (1) ask for filename  (2) ask for device  (3) write to ATA disk. */
static void op_save_file(void) {
    /* Step 1 — filename */
    char fn[56];
    if (!iedit_prompt("Save as (Enter=confirm, Esc=cancel): ", fn, 52)) {
        set_msg("Save cancelled.");
        return;
    }
    iedit_ensure_txt(fn, (int)sizeof(fn));

    /* Step 2 — device */
    set_msg("Save to: [1] Internal HDD (C:\\)  [2] External device (D:\\)  [Esc] Cancel");
    draw_msg();
    int choice = 0;
    for (;;) {
        int c = keyboard_getchar();
        if (c == '1') { choice = 1; break; }
        if (c == '2') { choice = 2; break; }
        if (c == KEY_ESCAPE) break;
    }
    if (!choice) {
        set_msg("Save cancelled.");
        return;
    }

    /* Build full path string for display */
    const char *drive = (choice == 1) ? "C:\\" : "D:\\";
    char path[80];
    int pi = 0;
    for (int i = 0; drive[i] && pi < 78; i++) path[pi++] = drive[i];
    for (int i = 0; fn[i]    && pi < 78; i++) path[pi++] = fn[i];
    path[pi] = '\0';

    /* Step 3 — serialise buffer → text (each line followed by '\n') */
    uint32_t bp = 0;
    for (int r = 0; r < nlines && bp < IEDIT_SAVE_BUF - 1; r++) {
        for (int c = 0; c < tlen[r] && bp < IEDIT_SAVE_BUF - 1; c++)
            iedit_save_buf[bp++] = (uint8_t)text[r][c];
        if (bp < IEDIT_SAVE_BUF - 1)
            iedit_save_buf[bp++] = '\n';
    }

    /* Step 4 — write to ATA drive */
    uint8_t drv = (uint8_t)(choice - 1);   /* choice 1 → drive 0, 2 → drive 1 */
    int rc = fs_write(drv, fn, iedit_save_buf, bp);

    /* Update editor state only on success */
    if (rc == FS_OK) {
        kstrncpy(fname, fn, 63); fname[63] = '\0';
        dirty = 0;
    }

    /* Step 5 — status message */
    char msg[SCR_COLS + 1];
    int ml = 0;
    if (rc == FS_OK) {
        const char *pfx = "Saved: ";
        for (int i = 0; pfx[i] && ml < SCR_COLS - 1; i++) msg[ml++] = pfx[i];
        for (int i = 0; path[i] && ml < SCR_COLS - 1; i++) msg[ml++] = path[i];
        const char *lbl = "  (";
        for (int i = 0; lbl[i] && ml < SCR_COLS - 1; i++) msg[ml++] = lbl[i];
        /* write nlines as decimal */
        char nbuf[8]; int ni = 0;
        int v = nlines;
        if (v == 0) { nbuf[ni++] = '0'; }
        else { while (v) { nbuf[ni++] = (char)('0' + v % 10); v /= 10; } }
        for (int a = 0, b = ni - 1; a < b; a++, b--)
            { char t = nbuf[a]; nbuf[a] = nbuf[b]; nbuf[b] = t; }
        for (int i = 0; i < ni && ml < SCR_COLS - 1; i++) msg[ml++] = nbuf[i];
        const char *sfx = " lines)";
        for (int i = 0; sfx[i] && ml < SCR_COLS - 1; i++) msg[ml++] = sfx[i];
    } else {
        const char *err =
            (rc == FS_ERR_NODRV) ? "Save failed: no drive found." :
            (rc == FS_ERR_FULL)  ? "Save failed: disk directory full." :
            (rc == FS_ERR_BIG)   ? "Save failed: file too large." :
                                   "Save failed: disk I/O error.";
        for (int i = 0; err[i] && ml < SCR_COLS - 1; i++) msg[ml++] = err[i];
    }
    msg[ml] = '\0';
    set_msg(msg);
}

/* ---- Open-file dialog (Ctrl+O) ---- */

/* Parse the raw byte buffer back into the editor's text[][] line store. */
static void iedit_parse_buf(uint32_t read_len) {
    for (int i = 0; i < MAX_LINES; i++) { text[i][0] = '\0'; tlen[i] = 0; }
    nlines = 0;

    int row = 0, col = 0;
    for (uint32_t i = 0; i < read_len && row < MAX_LINES; i++) {
        char c = (char)iedit_save_buf[i];
        if (c == '\n' || c == '\r') {
            text[row][col] = '\0';
            tlen[row] = col;
            row++;
            col = 0;
        } else if (col < MAX_COL) {
            text[row][col++] = c;
        }
    }
    /* Finalise last line if it had no trailing newline */
    if (col > 0 || row == 0) {
        text[row][col] = '\0';
        tlen[row] = col;
        row++;
    }
    nlines = (row > 0) ? row : 1;
}

/* Full open-from-file dialog triggered by Ctrl+O.
 * Steps: (1) ask for filename  (2) ask for device  (3) read from ATA disk. */
static void op_open_file(void) {
    /* Warn if there are unsaved changes */
    if (dirty) {
        set_msg("Unsaved changes! Ctrl+O again to discard and open.");
        draw_msg();
        /* Wait for confirmation */
        int c = keyboard_getchar();
        if (c != 0x0F) {        /* anything other than Ctrl+O = abort */
            set_msg("Open cancelled.");
            return;
        }
    }

    /* Step 1 — filename */
    char fn[56];
    if (!iedit_prompt("Open (.txt): ", fn, 52)) {
        set_msg("Open cancelled.");
        return;
    }
    iedit_ensure_txt(fn, (int)sizeof(fn));

    /* Step 2 — device */
    set_msg("Load from: [1] Internal HDD (C:\\)  [2] External device (D:\\)  [Esc] Cancel");
    draw_msg();
    int choice = 0;
    for (;;) {
        int c = keyboard_getchar();
        if (c == '1') { choice = 1; break; }
        if (c == '2') { choice = 2; break; }
        if (c == KEY_ESCAPE) break;
    }
    if (!choice) {
        set_msg("Open cancelled.");
        return;
    }

    /* Step 3 — read from ATA */
    uint8_t drv = (uint8_t)(choice - 1);
    uint32_t read_len = 0;
    int rc = fs_read(drv, fn, iedit_save_buf, IEDIT_SAVE_BUF - 1, &read_len);

    if (rc != FS_OK) {
        const char *err =
            (rc == FS_ERR_NOTFOUND) ? "Open failed: file not found." :
            (rc == FS_ERR_NODRV)    ? "Open failed: no drive found." :
                                      "Open failed: disk I/O error.";
        set_msg(err);
        return;
    }

    /* Step 4 — populate editor buffer */
    iedit_parse_buf(read_len);

    /* Step 5 — update editor state */
    kstrncpy(fname, fn, 63); fname[63] = '\0';
    cur_row = cur_col = view_top = dirty = 0;

    /* Build confirmation message: "Opened: C:\foo.txt  (N lines)" */
    char msg[SCR_COLS + 1];
    int ml = 0;
    const char *pfx = "Opened: ";
    for (int i = 0; pfx[i] && ml < SCR_COLS - 1; i++) msg[ml++] = pfx[i];
    const char *drive = (choice == 1) ? "C:\\" : "D:\\";
    for (int i = 0; drive[i] && ml < SCR_COLS - 1; i++) msg[ml++] = drive[i];
    for (int i = 0; fn[i]    && ml < SCR_COLS - 1; i++) msg[ml++] = fn[i];
    const char *lbl = "  (";
    for (int i = 0; lbl[i] && ml < SCR_COLS - 1; i++) msg[ml++] = lbl[i];
    /* write nlines as decimal */
    char nbuf[8]; int ni = 0;
    int v = nlines;
    if (v == 0) { nbuf[ni++] = '0'; }
    else { while (v) { nbuf[ni++] = (char)('0' + v % 10); v /= 10; } }
    for (int a = 0, b = ni - 1; a < b; a++, b--)
        { char t = nbuf[a]; nbuf[a] = nbuf[b]; nbuf[b] = t; }
    for (int i = 0; i < ni && ml < SCR_COLS - 1; i++) msg[ml++] = nbuf[i];
    const char *sfx = " lines)";
    for (int i = 0; sfx[i] && ml < SCR_COLS - 1; i++) msg[ml++] = sfx[i];
    msg[ml] = '\0';
    set_msg(msg);
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
    set_msg("iEdit  |  type 'save' or 'load' on a blank line  |  Ctrl+K: Del line  Ctrl+Q: Quit");
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

            /* Typed commands recognised only when the line contains exactly
             * that word (cursor just moved past the last character).        */
            char *ln = text[cur_row];
            int   ll = tlen[cur_row];

            /* "save" — save file */
            if (ll == 4 &&
                (ln[0]=='s'||ln[0]=='S') &&
                (ln[1]=='a'||ln[1]=='A') &&
                (ln[2]=='v'||ln[2]=='V') &&
                (ln[3]=='e'||ln[3]=='E')) {
                tlen[cur_row] = 0; ln[0] = '\0';
                op_save_file();
                scroll_view(); redraw(); continue;
            }

            /* "load" — open file */
            if (ll == 4 &&
                (ln[0]=='l'||ln[0]=='L') &&
                (ln[1]=='o'||ln[1]=='O') &&
                (ln[2]=='a'||ln[2]=='A') &&
                (ln[3]=='d'||ln[3]=='D')) {
                tlen[cur_row] = 0; ln[0] = '\0';
                op_open_file();
                scroll_view(); redraw(); continue;
            }

            /* "quit" — return to shell */
            if (ll == 4 &&
                (ln[0]=='q'||ln[0]=='Q') &&
                (ln[1]=='u'||ln[1]=='U') &&
                (ln[2]=='i'||ln[2]=='I') &&
                (ln[3]=='t'||ln[3]=='T')) {
                tlen[cur_row] = 0; ln[0] = '\0';
                break;
            }
        }

        scroll_view();
        redraw();
    }

    /* Return to shell: clear screen so the shell prompt redraws cleanly */
    vga_clear();
    vga_set_color(VGA_COLOR_WHITE, VGA_COLOR_BLACK);
}
