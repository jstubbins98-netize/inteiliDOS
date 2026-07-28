/*
 * inteilidOS -- shell/sheets.c
 * InteiliSheets 1.0 — full-screen spreadsheet application
 *
 * Screen layout (80×25 VGA text mode):
 *   Row  0      Title bar         (black on cyan)
 *   Row  1      Formula bar       "  [A1]:  <content>"
 *   Row  2      Column headers
 *   Rows 3-21   19 visible data rows (scrollable)
 *   Row 22      Horizontal rule
 *   Row 23      Status bar
 *   Row 24      Key-binding hints
 *
 * Grid: 7 columns (A–G), 50 rows, scrollable vertically.
 * Each cell stores up to 14 characters.
 * Formulas: =SUM(A1:G10)  =AVG(A1:A10)
 *
 * Keys (navigate mode):
 *   Arrow keys          Navigate
 *   Enter               Edit selected cell
 *   Q / Escape          Quit
 *   Delete / Ctrl+D     Clear selected cell
 *   Ctrl+Q              Quit
 *   Ctrl+S              "Save" (acknowledged — no real FS yet)
 *
 * Keys (edit mode):
 *   Enter               Commit edit, move cursor down
 *   Arrow keys          Commit edit, move cursor in that direction
 *   Escape / Ctrl+Q     Cancel edit / quit
 */

#include "sheets.h"
#include "../kernel/vga.h"
#include "../kernel/keyboard.h"
#include "../kernel/memory.h"
#include "../kernel/timer.h"
#include <stdint.h>
#include <stddef.h>

/* =========================================================================
 * Grid dimensions
 * ========================================================================= */
#define SH_COLS      7          /* columns A – G                           */
#define SH_ROWS     50          /* total rows (scrollable)                 */
#define SH_VIS      19          /* visible rows (screen rows 3-21)         */
#define SH_CMAX     14          /* max stored chars per cell               */
#define SH_CW        9          /* display chars per cell (excl. separator)*/

/* Screen geometry */
#define SH_TITLE_ROW  0
#define SH_FBAR_ROW   1
#define SH_HDR_ROW    2
#define SH_DATA_ROW   3         /* first data row on screen                */
#define SH_RULE_ROW  22
#define SH_STAT_ROW  23
#define SH_HINT_ROW  24

/* VGA direct access */
static volatile uint16_t *const SH_VGA = (volatile uint16_t *)0xB8000U;

#define SH_ENTRY(fg, bg, c) \
    ((uint16_t)(((bg) << 12) | ((fg) << 8) | (uint8_t)(c)))

static inline void sh_poke(int row, int col, char c,
                           vga_color_t fg, vga_color_t bg) {
    SH_VGA[row * 80 + col] = SH_ENTRY(fg, bg, c);
}

/* =========================================================================
 * Cell storage
 * ========================================================================= */
typedef struct {
    char    str[SH_CMAX + 1];  /* raw content (what the user typed)        */
    int32_t val;               /* evaluated integer value                  */
    uint8_t is_num;            /* 1 = val is valid                         */
    uint8_t is_formula;        /* 1 = content begins with '='              */
} ShCell;

static ShCell sh_cells[SH_ROWS][SH_COLS];
static int sh_dirty;           /* unsaved-changes flag                     */
static int sh_cur_col;         /* current cursor column (0-6)              */
static int sh_cur_row;         /* current cursor row (0-49)                */
static int sh_scroll;          /* index of top visible row                 */

/* =========================================================================
 * String helpers (avoid relying on full libc)
 * ========================================================================= */
static int sh_isdigit(char c)   { return c >= '0' && c <= '9'; }
static int sh_isupper(char c)   { return c >= 'A' && c <= 'Z'; }
static int sh_toupper(char c)   { return (c >= 'a' && c <= 'z') ? c - 32 : c; }
static int sh_strlen(const char *s) {
    int n = 0; while (s[n]) n++; return n;
}

/* Write a fixed-width padded string into VGA at (row, start_col). */
static void sh_puts_w(int row, int col, const char *s, int width,
                      vga_color_t fg, vga_color_t bg) {
    int i = 0;
    while (i < width) {
        char c = s && s[i] ? s[i] : ' ';
        sh_poke(row, col + i, c, fg, bg);
        i++;
    }
}

/* Write a decimal integer right-aligned in a field of 'width'. */
static void sh_put_int(int row, int col, int32_t v, int width,
                       vga_color_t fg, vga_color_t bg) {
    char buf[16];
    int neg = 0;
    if (v < 0) { neg = 1; v = -v; }
    int i = 0;
    do { buf[i++] = (char)('0' + v % 10); v /= 10; } while (v);
    if (neg) buf[i++] = '-';
    /* right-align: pad left with spaces, then print digits in reverse */
    int len = i;
    int pad = width - len;
    int c = col;
    for (int p = 0; p < pad; p++) sh_poke(row, c++, ' ', fg, bg);
    for (int d = len - 1; d >= 0; d--) sh_poke(row, c++, buf[d], fg, bg);
}

/* =========================================================================
 * Numeric parsing
 * ========================================================================= */
/* Parse a decimal integer (optionally negative).  Returns 1 on success. */
static int sh_parse_int(const char *s, int32_t *out) {
    if (!s || !s[0]) return 0;
    int i = 0;
    int neg = 0;
    if (s[i] == '-') { neg = 1; i++; }
    if (!sh_isdigit(s[i])) return 0;
    int32_t v = 0;
    while (sh_isdigit(s[i])) { v = v * 10 + (s[i] - '0'); i++; }
    if (s[i] != '\0') return 0;   /* trailing non-digit → not a pure integer */
    *out = neg ? -v : v;
    return 1;
}

/* =========================================================================
 * Formula evaluation
 * ========================================================================= */

/* Parse a cell reference like "A1", "G50" → col (0-6), row (0-49).
 * Returns 1 on success.
 */
static int sh_parse_ref(const char *s, int *col, int *row) {
    if (!s) return 0;
    char c0 = sh_toupper(s[0]);
    if (!sh_isupper(c0)) return 0;
    *col = c0 - 'A';
    if (*col < 0 || *col >= SH_COLS) return 0;
    int r = 0, i = 1;
    if (!sh_isdigit(s[i])) return 0;
    while (sh_isdigit(s[i])) { r = r * 10 + (s[i] - '0'); i++; }
    if (s[i] != '\0') return 0;
    r--;  /* 1-based → 0-based */
    if (r < 0 || r >= SH_ROWS) return 0;
    *row = r;
    return 1;
}

/* Get the integer value of a cell (recursively evaluates formulas).
 * depth limits formula recursion to prevent infinite loops.
 * Returns 1 if the cell has a numeric value.
 */
static int sh_cell_val(int col, int row, int32_t *out, int depth);

static int sh_eval_formula(int fc, int fr, int32_t *out, int depth) {
    if (depth > 8) return 0;
    const char *f = sh_cells[fr][fc].str;

    /* Skip '=' */
    if (f[0] != '=') return 0;
    f++;

    /* Detect SUM( or AVG( */
    int is_avg = 0;
    const char *body = (const char *)0;

    /* Manual prefix checks */
    if (f[0]=='S' && f[1]=='U' && f[2]=='M' && f[3]=='(') { body = f + 4; }
    else if (f[0]=='A' && f[1]=='V' && f[2]=='G' && f[3]=='(') { body = f + 4; is_avg = 1; }
    else return 0;

    /* Find closing ')' */
    char range_buf[32];
    int ri = 0;
    while (body[ri] && body[ri] != ')' && ri < 31) {
        range_buf[ri] = body[ri]; ri++;
    }
    range_buf[ri] = '\0';

    /* Split on ':' to get two cell refs */
    char ref1[8], ref2[8];
    int sep = -1;
    for (int i = 0; range_buf[i]; i++) {
        if (range_buf[i] == ':') { sep = i; break; }
    }
    if (sep < 0 || sep >= 7) return 0;

    for (int i = 0; i < sep && i < 7; i++) ref1[i] = range_buf[i];
    ref1[sep] = '\0';
    int j = 0;
    for (int i = sep + 1; range_buf[i] && j < 7; i++) ref2[j++] = range_buf[i];
    ref2[j] = '\0';

    int c1, r1, c2, r2;
    if (!sh_parse_ref(ref1, &c1, &r1)) return 0;
    if (!sh_parse_ref(ref2, &c2, &r2)) return 0;

    /* Normalise range */
    if (c1 > c2) { int t = c1; c1 = c2; c2 = t; }
    if (r1 > r2) { int t = r1; r1 = r2; r2 = t; }
    if (c2 >= SH_COLS) c2 = SH_COLS - 1;
    if (r2 >= SH_ROWS) r2 = SH_ROWS - 1;

    int32_t sum = 0;
    int count = 0;
    for (int row = r1; row <= r2; row++) {
        for (int col = c1; col <= c2; col++) {
            int32_t v;
            if (sh_cell_val(col, row, &v, depth + 1)) {
                sum += v;
                count++;
            }
        }
    }
    if (is_avg) {
        *out = count ? (sum / count) : 0;
    } else {
        *out = sum;
    }
    return 1;
}

static int sh_cell_val(int col, int row, int32_t *out, int depth) {
    if (col < 0 || col >= SH_COLS || row < 0 || row >= SH_ROWS) return 0;
    ShCell *c = &sh_cells[row][col];
    if (c->str[0] == '\0') return 0;
    if (c->is_formula) return sh_eval_formula(col, row, out, depth);
    if (c->is_num) { *out = c->val; return 1; }
    return 0;
}

/* Update the cached is_num / val / is_formula for a cell after editing. */
static void sh_update_cell(int col, int row) {
    ShCell *c = &sh_cells[row][col];
    c->is_formula = (c->str[0] == '=') ? 1 : 0;
    c->is_num = 0;
    c->val = 0;
    if (!c->is_formula) {
        int32_t v;
        if (sh_parse_int(c->str, &v)) {
            c->val = v;
            c->is_num = 1;
        }
    } else {
        int32_t v;
        if (sh_eval_formula(col, row, &v, 0)) {
            c->val = v;
            c->is_num = 1;
        }
    }
}

/* =========================================================================
 * Rendering helpers
 * ========================================================================= */
static void sh_fill_row(int screen_row, vga_color_t fg, vga_color_t bg) {
    for (int c = 0; c < 80; c++) sh_poke(screen_row, c, ' ', fg, bg);
}

static void sh_draw_title(void) {
    sh_fill_row(SH_TITLE_ROW, VGA_COLOR_BLACK, VGA_COLOR_LIGHT_CYAN);
    const char *title = "  InteiliSheets 1.0";
    for (int i = 0; title[i]; i++)
        sh_poke(SH_TITLE_ROW, i, title[i], VGA_COLOR_BLACK, VGA_COLOR_LIGHT_CYAN);
    const char *hints = "  Q=Quit  Ctrl+S=Save  ENTER=Edit  DEL=Clear  ";
    int hlen = sh_strlen(hints);
    int hcol = 80 - hlen;
    for (int i = 0; hints[i]; i++)
        sh_poke(SH_TITLE_ROW, hcol + i, hints[i], VGA_COLOR_BLACK, VGA_COLOR_CYAN);
}

static void sh_draw_fbar(void) {
    /* Formula bar: "  [A1]:  <content>"  */
    sh_fill_row(SH_FBAR_ROW, VGA_COLOR_WHITE, VGA_COLOR_BLACK);

    /* Cell address */
    char addr[6];
    addr[0] = ' '; addr[1] = '[';
    addr[2] = (char)('A' + sh_cur_col);
    /* row digits */
    int r = sh_cur_row + 1;
    if (r >= 10) { addr[3] = (char)('0' + r / 10); addr[4] = (char)('0' + r % 10); addr[5] = '\0'; }
    else         { addr[3] = (char)('0' + r);       addr[4] = '\0'; }
    for (int i = 0; addr[i]; i++)
        sh_poke(SH_FBAR_ROW, i, addr[i], VGA_COLOR_LIGHT_CYAN, VGA_COLOR_BLACK);
    sh_poke(SH_FBAR_ROW, sh_strlen(addr), ']', VGA_COLOR_LIGHT_CYAN, VGA_COLOR_BLACK);
    sh_poke(SH_FBAR_ROW, sh_strlen(addr) + 1, ':', VGA_COLOR_LIGHT_CYAN, VGA_COLOR_BLACK);
    sh_poke(SH_FBAR_ROW, sh_strlen(addr) + 2, ' ', VGA_COLOR_WHITE, VGA_COLOR_BLACK);

    /* Cell content */
    const char *s = sh_cells[sh_cur_row][sh_cur_col].str;
    int start = sh_strlen(addr) + 3;
    for (int i = 0; s[i] && start + i < 80; i++)
        sh_poke(SH_FBAR_ROW, start + i, s[i], VGA_COLOR_WHITE, VGA_COLOR_BLACK);
}

static void sh_draw_col_header(void) {
    sh_fill_row(SH_HDR_ROW, VGA_COLOR_BLACK, VGA_COLOR_LIGHT_GREY);
    /* Row-number gutter header: " Row" */
    const char *gh = " Row";
    for (int i = 0; gh[i]; i++)
        sh_poke(SH_HDR_ROW, i, gh[i], VGA_COLOR_BLACK, VGA_COLOR_LIGHT_GREY);
    sh_poke(SH_HDR_ROW, 4, '|', VGA_COLOR_BLACK, VGA_COLOR_LIGHT_GREY);
    /* Column letters */
    for (int c = 0; c < SH_COLS; c++) {
        int base = 5 + c * (SH_CW + 1);
        /* Centre the letter in the cell */
        for (int i = 0; i < SH_CW; i++)
            sh_poke(SH_HDR_ROW, base + i, ' ', VGA_COLOR_BLACK, VGA_COLOR_LIGHT_GREY);
        sh_poke(SH_HDR_ROW, base + SH_CW / 2, (char)('A' + c),
                VGA_COLOR_BLACK, VGA_COLOR_LIGHT_GREY);
        if (c < SH_COLS - 1)
            sh_poke(SH_HDR_ROW, base + SH_CW, '|',
                    VGA_COLOR_BLACK, VGA_COLOR_LIGHT_GREY);
    }
}

/* Draw a single data row at screen row screen_r. */
static void sh_draw_row(int data_row, int screen_r) {
    int is_cur_row = (data_row == sh_cur_row);

    vga_color_t row_bg = is_cur_row ? VGA_COLOR_BLUE : VGA_COLOR_BLACK;
    vga_color_t row_fg = is_cur_row ? VGA_COLOR_WHITE : VGA_COLOR_LIGHT_GREY;

    sh_fill_row(screen_r, row_fg, row_bg);

    /* Row number */
    int r = data_row + 1;
    char rn[4];
    rn[0] = ' ';
    if (r < 10)        { rn[1] = ' '; rn[2] = (char)('0' + r); rn[3] = '\0'; }
    else if (r < 100)  { rn[1] = (char)('0' + r/10); rn[2] = (char)('0' + r%10); rn[3] = '\0'; }
    else               { rn[1] = '?'; rn[2] = '?'; rn[3] = '\0'; }
    for (int i = 0; rn[i]; i++)
        sh_poke(screen_r, i, rn[i], VGA_COLOR_DARK_GREY, row_bg);
    sh_poke(screen_r, 4, '|', VGA_COLOR_DARK_GREY, row_bg);

    /* Cells */
    for (int c = 0; c < SH_COLS; c++) {
        int base = 5 + c * (SH_CW + 1);
        int is_cur = is_cur_row && (c == sh_cur_col);

        vga_color_t cfg = is_cur ? VGA_COLOR_BLACK : VGA_COLOR_WHITE;
        vga_color_t cbg = is_cur ? VGA_COLOR_LIGHT_CYAN : row_bg;

        ShCell *cell = &sh_cells[data_row][c];

        if (cell->str[0] == '\0') {
            /* Empty cell */
            for (int i = 0; i < SH_CW; i++)
                sh_poke(screen_r, base + i, ' ', cfg, cbg);
        } else if (cell->is_num || cell->is_formula) {
            /* Numeric: right-align the value */
            int32_t v = 0;
            sh_cell_val(c, data_row, &v, 0);
            sh_put_int(screen_r, base, v, SH_CW, cfg, cbg);
        } else {
            /* Text: left-align, truncate */
            sh_puts_w(screen_r, base, cell->str, SH_CW, cfg, cbg);
        }

        if (c < SH_COLS - 1)
            sh_poke(screen_r, base + SH_CW, '|', VGA_COLOR_DARK_GREY, row_bg);
    }
}

static void sh_draw_rule(void) {
    sh_fill_row(SH_RULE_ROW, VGA_COLOR_DARK_GREY, VGA_COLOR_BLACK);
    for (int c = 0; c < 80; c++)
        sh_poke(SH_RULE_ROW, c, '-', VGA_COLOR_DARK_GREY, VGA_COLOR_BLACK);
}

static void sh_draw_status(const char *msg) {
    sh_fill_row(SH_STAT_ROW, VGA_COLOR_BLACK, VGA_COLOR_LIGHT_GREY);
    for (int i = 0; msg[i] && i < 80; i++)
        sh_poke(SH_STAT_ROW, i, msg[i], VGA_COLOR_BLACK, VGA_COLOR_LIGHT_GREY);

    /* Dirty indicator */
    if (sh_dirty) {
        const char *d = " [MODIFIED]";
        int dc = 80 - sh_strlen(d);
        for (int i = 0; d[i]; i++)
            sh_poke(SH_STAT_ROW, dc + i, d[i], VGA_COLOR_LIGHT_RED, VGA_COLOR_LIGHT_GREY);
    }
}

static void sh_draw_hints(void) {
    sh_fill_row(SH_HINT_ROW, VGA_COLOR_DARK_GREY, VGA_COLOR_BLACK);
    const char *h = "  ARROWS=Navigate  ENTER=Edit  DEL=Clear  "
                    "=SUM(A1:G10) / =AVG(A1:A10)  Q/Esc=Quit";
    for (int i = 0; h[i] && i < 80; i++)
        sh_poke(SH_HINT_ROW, i, h[i], VGA_COLOR_DARK_GREY, VGA_COLOR_BLACK);
}

/* Full redraw of the visible grid. */
static void sh_redraw_all(const char *status_msg) {
    sh_draw_title();
    sh_draw_fbar();
    sh_draw_col_header();
    for (int i = 0; i < SH_VIS; i++) {
        int dr = sh_scroll + i;
        int sr = SH_DATA_ROW + i;
        if (dr < SH_ROWS) sh_draw_row(dr, sr);
        else              sh_fill_row(sr, VGA_COLOR_BLACK, VGA_COLOR_BLACK);
    }
    sh_draw_rule();
    sh_draw_status(status_msg);
    sh_draw_hints();
}

/* =========================================================================
 * Cell editing
 * ========================================================================= */
/*
 * sh_edit_cell() return codes:
 *   SH_ED_DONE  — edit committed or cancelled; stay on current cell
 *   SH_ED_QUIT  — user wants to quit InteiliSheets entirely
 *   SH_ED_UP    — committed; caller should move cursor up
 *   SH_ED_DOWN  — committed; caller should move cursor down
 *   SH_ED_LEFT  — committed; caller should move cursor left
 *   SH_ED_RIGHT — committed; caller should move cursor right
 */
#define SH_ED_DONE  0
#define SH_ED_QUIT  1
#define SH_ED_UP    2
#define SH_ED_DOWN  3
#define SH_ED_LEFT  4
#define SH_ED_RIGHT 5

/* Commit buf into the cell and update cached values. */
static void sh_commit_cell(char *buf, int len) {
    ShCell *cell = &sh_cells[sh_cur_row][sh_cur_col];
    buf[len] = '\0';
    for (int i = 0; i <= SH_CMAX; i++) cell->str[i] = buf[i];
    sh_update_cell(sh_cur_col, sh_cur_row);
    sh_dirty = 1;
}

static int sh_edit_cell(void) {
    ShCell *cell = &sh_cells[sh_cur_row][sh_cur_col];
    char buf[SH_CMAX + 1];
    for (int i = 0; i <= SH_CMAX; i++) buf[i] = cell->str[i];
    int len = sh_strlen(buf);

    sh_draw_status("  EDITING -- Enter/Arrows=Accept  Esc=Cancel  Ctrl+Q/Q=Quit");

    while (1) {
        /* Redraw formula bar */
        sh_fill_row(SH_FBAR_ROW, VGA_COLOR_WHITE, VGA_COLOR_BLACK);
        const char *pfx = "  EDIT: ";
        for (int i = 0; pfx[i]; i++)
            sh_poke(SH_FBAR_ROW, i, pfx[i], VGA_COLOR_LIGHT_CYAN, VGA_COLOR_BLACK);
        int ec = sh_strlen(pfx);
        for (int i = 0; i < len; i++)
            sh_poke(SH_FBAR_ROW, ec + i, buf[i], VGA_COLOR_WHITE, VGA_COLOR_BLACK);
        sh_poke(SH_FBAR_ROW, ec + len, '_', VGA_COLOR_LIGHT_GREEN, VGA_COLOR_BLACK);

        int k = keyboard_getchar();

        /* Commit + directional navigation */
        if (k == KEY_UP)    { sh_commit_cell(buf, len); return SH_ED_UP;    }
        if (k == KEY_DOWN)  { sh_commit_cell(buf, len); return SH_ED_DOWN;  }
        if (k == KEY_LEFT)  { sh_commit_cell(buf, len); return SH_ED_LEFT;  }
        if (k == KEY_RIGHT) { sh_commit_cell(buf, len); return SH_ED_RIGHT; }

        /* Enter — commit and stay (caller advances down) */
        if (k == KEY_ENTER || k == '\r') {
            sh_commit_cell(buf, len);
            return SH_ED_DONE;
        }

        /* Escape — cancel edit, stay on cell */
        if (k == KEY_ESCAPE) {
            return SH_ED_DONE;
        }

        /* Ctrl+Q or Q — quit InteiliSheets */
        if (k == 0x11 || k == 'q' || k == 'Q') {
            return SH_ED_QUIT;
        }

        /* Backspace */
        if (k == KEY_BACKSPACE && len > 0) {
            buf[--len] = '\0';
            continue;
        }

        /* Printable character */
        if (k >= 0x20 && k < 0x7F && len < SH_CMAX) {
            buf[len++] = (char)k;
            buf[len]   = '\0';
        }
    }
}

/* =========================================================================
 * Main entry point
 * ========================================================================= */
void sheets_run(void) {
    /* Initialise state */
    kmemset(sh_cells, 0, sizeof(sh_cells));
    sh_cur_col = 0;
    sh_cur_row = 0;
    sh_scroll  = 0;
    sh_dirty   = 0;

    vga_clear();
    sh_redraw_all("  Ready");

    while (1) {
        vga_set_cursor(SH_DATA_ROW + (sh_cur_row - sh_scroll), 5 + sh_cur_col * (SH_CW + 1));

        int k = keyboard_getchar();

        if (k == 0x11) break;   /* Ctrl+Q — quit */

        if (k == 0x13) {
            /* Ctrl+S — acknowledge save */
            sh_dirty = 0;
            sh_redraw_all("  Saved (in-memory — no disk driver yet)");
            timer_sleep(1200);
            sh_redraw_all("  Ready");
            continue;
        }

        if (k == KEY_UP) {
            if (sh_cur_row > 0) {
                sh_cur_row--;
                if (sh_cur_row < sh_scroll) {
                    sh_scroll = sh_cur_row;
                    sh_redraw_all("  Ready");
                } else {
                    /* Redraw only old and new current rows */
                    sh_draw_row(sh_cur_row + 1, SH_DATA_ROW + (sh_cur_row + 1 - sh_scroll));
                    sh_draw_row(sh_cur_row,     SH_DATA_ROW + (sh_cur_row     - sh_scroll));
                    sh_draw_fbar();
                }
            }
            continue;
        }

        if (k == KEY_DOWN) {
            if (sh_cur_row < SH_ROWS - 1) {
                sh_cur_row++;
                if (sh_cur_row >= sh_scroll + SH_VIS) {
                    sh_scroll = sh_cur_row - SH_VIS + 1;
                    sh_redraw_all("  Ready");
                } else {
                    sh_draw_row(sh_cur_row - 1, SH_DATA_ROW + (sh_cur_row - 1 - sh_scroll));
                    sh_draw_row(sh_cur_row,     SH_DATA_ROW + (sh_cur_row     - sh_scroll));
                    sh_draw_fbar();
                }
            }
            continue;
        }

        if (k == KEY_LEFT) {
            if (sh_cur_col > 0) {
                int old = sh_cur_col--;
                sh_draw_row(sh_cur_row, SH_DATA_ROW + (sh_cur_row - sh_scroll));
                sh_draw_fbar();
                (void)old;
            }
            continue;
        }

        if (k == KEY_RIGHT) {
            if (sh_cur_col < SH_COLS - 1) {
                sh_cur_col++;
                sh_draw_row(sh_cur_row, SH_DATA_ROW + (sh_cur_row - sh_scroll));
                sh_draw_fbar();
            }
            continue;
        }

        /* Q / Escape / Ctrl+Q — quit from navigation mode */
        if (k == 'q' || k == 'Q' || k == KEY_ESCAPE || k == 0x11) break;

        if (k == KEY_ENTER || k == '\r') {
            int ed = sh_edit_cell();
            if (ed == SH_ED_QUIT) break;
            sh_redraw_all("  Ready");
            /* After Enter, advance down (same as before) */
            if (ed == SH_ED_DONE && sh_cur_row < SH_ROWS - 1) {
                sh_cur_row++;
                if (sh_cur_row >= sh_scroll + SH_VIS) {
                    sh_scroll = sh_cur_row - SH_VIS + 1;
                    sh_redraw_all("  Ready");
                } else {
                    sh_draw_row(sh_cur_row - 1, SH_DATA_ROW + (sh_cur_row - 1 - sh_scroll));
                    sh_draw_row(sh_cur_row,     SH_DATA_ROW + (sh_cur_row     - sh_scroll));
                    sh_draw_fbar();
                }
            } else if (ed == SH_ED_UP   && sh_cur_row > 0)            { sh_cur_row--; sh_redraw_all("  Ready"); }
            else if (ed == SH_ED_DOWN  && sh_cur_row < SH_ROWS - 1)   { sh_cur_row++; sh_redraw_all("  Ready"); }
            else if (ed == SH_ED_LEFT  && sh_cur_col > 0)              { sh_cur_col--; sh_redraw_all("  Ready"); }
            else if (ed == SH_ED_RIGHT && sh_cur_col < SH_COLS - 1)   { sh_cur_col++; sh_redraw_all("  Ready"); }
            continue;
        }

        /* Delete / Ctrl+D — clear cell */
        if (k == 0x7F || k == 0x04) {
            ShCell *cell = &sh_cells[sh_cur_row][sh_cur_col];
            kmemset(cell, 0, sizeof(ShCell));
            sh_dirty = 1;
            sh_draw_row(sh_cur_row, SH_DATA_ROW + (sh_cur_row - sh_scroll));
            sh_draw_fbar();
            sh_draw_status("  Cell cleared");
            continue;
        }

        /* Typing a printable character starts immediate edit */
        if (k >= 0x20 && k < 0x7F) {
            ShCell *cell = &sh_cells[sh_cur_row][sh_cur_col];
            kmemset(cell, 0, sizeof(ShCell));
            cell->str[0] = (char)k;
            cell->str[1] = '\0';
            sh_draw_fbar();
            int ed = sh_edit_cell();
            if (ed == SH_ED_QUIT) break;
            sh_redraw_all("  Ready");
            /* Advance after typing */
            if (sh_cur_row < SH_ROWS - 1) {
                sh_cur_row++;
                if (sh_cur_row >= sh_scroll + SH_VIS) sh_scroll = sh_cur_row - SH_VIS + 1;
                sh_redraw_all("  Ready");
            }
            continue;
        }
    }

    /* Restore VGA state and return to shell */
    vga_clear();
    vga_set_color(VGA_COLOR_WHITE, VGA_COLOR_BLACK);
    vga_set_cursor(0, 0);
}
