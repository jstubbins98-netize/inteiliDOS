/*
 * inteiliDOS -- shell/filemanager.c
 * InteiliFile Manager 1.0
 *
 * Full-screen graphical file system manager modelled on the classic
 * Norton Commander / Midnight Commander aesthetic.
 *
 * Screen layout (80x25 VGA text mode):
 *   Row  0    Title bar            (black on cyan)
 *   Row  1    Path bar             (white on dark-grey)
 *   Row  2    Column headers       (white on blue)
 *   Rows 3-20 18 scrollable entries
 *   Row 21    Separator line
 *   Row 22    Selected-item info bar
 *   Row 23    Key-binding hints
 *   Row 24    Brand footer
 *
 * Columns:
 *   0     leading space
 *   1-21  Name  (21 chars — icon + name)
 *   22-29 Size  (8 chars, right-aligned; "<DIR>" for dirs)
 *   30    space
 *   31-35 Type  (5)
 *   36    space
 *   37-44 Date  (8)
 *   45    space
 *   46-79 Description (34)
 *
 * State machine:
 *   BROWSE   — arrow keys, Enter, Backspace, Q
 *   MENU     — file action popup (arrow keys + Enter)
 *   INFO     — properties dialog (any key closes)
 *   RENAME   — text input (Enter confirm, Esc cancel)
 *   CONFIRM  — delete confirm (Enter / Esc)
 */

#include "filemanager.h"
#include "../kernel/vga.h"
#include "../kernel/keyboard.h"
#include <stdint.h>
#include <stddef.h>

/* =========================================================================
 * VGA direct writes
 * ========================================================================= */
static volatile uint16_t *const FM_VGA = (volatile uint16_t *)0xB8000U;

static inline void fm_poke(int row, int col, unsigned char c,
                            vga_color_t fg, vga_color_t bg) {
    if ((unsigned)row >= 25 || (unsigned)col >= 80) return;
    FM_VGA[row * 80 + col] =
        (uint16_t)(((uint8_t)bg << 12) | ((uint8_t)fg << 8) | c);
}

static void fm_fill(int row, int col, unsigned char c,
                    vga_color_t fg, vga_color_t bg, int n) {
    for (int i = 0; i < n; i++) fm_poke(row, col + i, c, fg, bg);
}

/* Write a string left-justified in a field of max_w, space-padded. */
static void fm_puts(int row, int col, const char *s,
                    vga_color_t fg, vga_color_t bg, int max_w) {
    int i = 0;
    for (; s && s[i] && i < max_w; i++) fm_poke(row, col+i, (unsigned char)s[i], fg, bg);
    for (; i < max_w; i++)             fm_poke(row, col+i, ' ', fg, bg);
}

/* Write uint32_t right-aligned in a field of w. */
static void fm_put_uint(int row, int col, uint32_t v, int w,
                        vga_color_t fg, vga_color_t bg) {
    char buf[12]; int n = 0;
    if (!v) buf[n++] = '0';
    else { while (v) { buf[n++] = (char)('0' + v % 10); v /= 10; } }
    int pad = w - n;
    for (int p = 0; p < pad; p++) fm_poke(row, col++, ' ', fg, bg);
    for (int d = n - 1; d >= 0; d--) fm_poke(row, col++, (unsigned char)buf[d], fg, bg);
}

/* Convert uint32_t to null-terminated string. */
static int fm_uint_to_str(uint32_t v, char *out) {
    if (!v) { out[0]='0'; out[1]='\0'; return 1; }
    char tmp[12]; int n = 0;
    while (v) { tmp[n++] = (char)('0' + v % 10); v /= 10; }
    for (int i = 0; i < n; i++) out[i] = tmp[n-1-i];
    out[n] = '\0';
    return n;
}

/* =========================================================================
 * String helpers
 * ========================================================================= */
static int fm_strlen(const char *s) {
    int n = 0; while (s && s[n]) n++; return n;
}
static void fm_strcpy(char *d, const char *s, int max) {
    int i;
    for (i = 0; s[i] && i < max - 1; i++) d[i] = s[i];
    d[i] = '\0';
}
static int fm_strcmp(const char *a, const char *b) {
    while (*a && *b && *a == *b) { a++; b++; }
    return (unsigned char)*a - (unsigned char)*b;
}

/* =========================================================================
 * CP-437 box-drawing characters
 * ========================================================================= */
#define BOX_TL  '\xDA'  /* ┌ */
#define BOX_TR  '\xBF'  /* ┐ */
#define BOX_BL  '\xC0'  /* └ */
#define BOX_BR  '\xD9'  /* ┘ */
#define BOX_H   '\xC4'  /* ─ */
#define BOX_V   '\xB3'  /* │ */
#define BOX_ML  '\xC3'  /* ├ */
#define BOX_MR  '\xB4'  /* ┤ */

static void fm_draw_box(int row, int col, int h, int w,
                        vga_color_t fg, vga_color_t bg) {
    fm_poke(row,       col,       BOX_TL, fg, bg);
    fm_fill(row,       col+1,     BOX_H,  fg, bg, w-2);
    fm_poke(row,       col+w-1,   BOX_TR, fg, bg);
    for (int r = row+1; r < row+h-1; r++) {
        fm_poke(r,     col,       BOX_V,  fg, bg);
        fm_fill(r,     col+1,     ' ',    fg, bg, w-2);
        fm_poke(r,     col+w-1,   BOX_V,  fg, bg);
    }
    fm_poke(row+h-1,   col,       BOX_BL, fg, bg);
    fm_fill(row+h-1,   col+1,     BOX_H,  fg, bg, w-2);
    fm_poke(row+h-1,   col+w-1,   BOX_BR, fg, bg);
}

/* Horizontal divider inside an existing box. */
static void fm_box_divider(int row, int col, int w,
                            vga_color_t fg, vga_color_t bg) {
    fm_poke(row, col,       BOX_ML, fg, bg);
    fm_fill(row, col+1,     BOX_H,  fg, bg, w-2);
    fm_poke(row, col+w-1,   BOX_MR, fg, bg);
}

/* =========================================================================
 * Virtual filesystem
 * ========================================================================= */
#define FM_NLEN   20
#define FM_DLEN   32
#define FM_ELEN    5
#define FM_PLEN   48
#define FM_MAXE   14
#define FM_MAXD   14

typedef struct {
    char     name[FM_NLEN];
    uint8_t  is_dir;
    uint8_t  deleted;
    uint32_t size;
    char     date[9];
    char     ext[FM_ELEN];
    char     desc[FM_DLEN];
} fm_entry_t;

typedef struct {
    char       path[FM_PLEN];
    fm_entry_t e[FM_MAXE];
    int        count;
} fm_dir_t;

static fm_dir_t fm_dirs[FM_MAXD];
static int      fm_ndirs;

static void fm_init_fs(void) {
    fm_ndirs = 0;

#define D(p)   do { fm_strcpy(fm_dirs[fm_ndirs].path,(p),FM_PLEN); \
                    fm_dirs[fm_ndirs].count=0; fm_ndirs++; } while(0)
#define E(idx,nm,isdir,sz,dt,ex,dc) do { \
    fm_entry_t *_e=&fm_dirs[(idx)].e[fm_dirs[(idx)].count++]; \
    fm_strcpy(_e->name,(nm),FM_NLEN); _e->is_dir=(isdir); _e->deleted=0; \
    _e->size=(sz); fm_strcpy(_e->date,(dt),9); \
    fm_strcpy(_e->ext,(ex),FM_ELEN); fm_strcpy(_e->desc,(dc),FM_DLEN); } while(0)

    /* 0: C:\ */
    D("C:\\");
    E( 0,"BIN",         1,     0,"07/27/26","DIR","System binaries");
    E( 0,"CONFIG",      1,     0,"07/27/26","DIR","Configuration files");
    E( 0,"APPS",        1,     0,"07/27/26","DIR","User applications");
    E( 0,"USERS",       1,     0,"07/27/26","DIR","User home directories");
    E( 0,"SYSTEM",      1,     0,"07/27/26","DIR","Core system files");
    E( 0,"PACKAGES",    1,     0,"07/27/26","DIR","Installed packages");
    E( 0,"TEMP",        1,     0,"07/27/26","DIR","Temporary files");
    E( 0,"LOGS",        1,     0,"07/27/26","DIR","System log files");
    E( 0,"README.TXT",  0,   243,"07/27/26","TXT","System documentation");
    E( 0,"AUTOEXEC.BAT",0,   128,"07/27/26","BAT","Startup batch script");

    /* 1: C:\BIN\ */
    D("C:\\BIN");
    E( 1,"KERNEL.BIN",  0, 47616,"07/27/26","BIN","OS kernel image");
    E( 1,"SHELL.EXE",   0, 32768,"07/27/26","EXE","IntelliShell executable");
    E( 1,"VGA.DRV",     0,  8192,"07/27/26","DRV","VGA text-mode driver");
    E( 1,"KEYBOARD.DRV",0,  4096,"07/27/26","DRV","PS/2 keyboard driver");
    E( 1,"TIMER.DRV",   0,  3072,"07/27/26","DRV","PIT 8254 timer driver");
    E( 1,"USB.DRV",     0, 12288,"07/27/26","DRV","UHCI USB HID driver");
    E( 1,"MEMORY.EXE",  0,  6144,"07/27/26","EXE","Memory manager");

    /* 2: C:\CONFIG\ */
    D("C:\\CONFIG");
    E( 2,"SYSTEM.INI",  0,   512,"07/27/26","INI","System configuration");
    E( 2,"GRUB.CFG",    0,  1024,"07/27/26","CFG","GRUB2 boot menu config");
    E( 2,"BOOT.CFG",    0,   256,"07/27/26","CFG","Kernel boot parameters");
    E( 2,"AUTOEXEC.BAT",0,   128,"07/27/26","BAT","Autoexec startup script");
    E( 2,"KEYBOARD.CFG",0,    64,"07/27/26","CFG","Keyboard layout map");

    /* 3: C:\APPS\ */
    D("C:\\APPS");
    E( 3,"IEDIT.EXE",   0, 28672,"07/27/26","EXE","IEdit text editor");
    E( 3,"BASIC.EXE",   0, 65536,"07/27/26","EXE","InteiliBASIC interpreter");
    E( 3,"SHEETS.EXE",  0, 24576,"07/27/26","EXE","InteiliSheets spreadsheet");
    E( 3,"TALK.EXE",    0, 16384,"07/27/26","EXE","InteiliTalk TTS engine");
    E( 3,"FILEMAN.EXE", 0, 20480,"07/27/26","EXE","InteiliFile Manager");
    E( 3,"TOUR.EXE",    0, 12288,"07/27/26","EXE","TOUR text adventure");
    E( 3,"DEMO.EXE",    0,  8192,"07/27/26","EXE","Feature showcase demo");

    /* 4: C:\USERS\ */
    D("C:\\USERS");
    E( 4,"ADMIN",       1,     0,"07/27/26","DIR","Administrator account");
    E( 4,"GUEST",       1,     0,"07/27/26","DIR","Guest account");
    E( 4,"SHARED",      1,     0,"07/27/26","DIR","Shared files");

    /* 5: C:\USERS\ADMIN\ */
    D("C:\\USERS\\ADMIN");
    E( 5,"PROFILE.DAT", 0,   256,"07/27/26","DAT","User profile data");
    E( 5,"HISTORY.LOG", 0,  1024,"07/27/26","LOG","Command history log");
    E( 5,"DESKTOP.INI", 0,   128,"07/27/26","INI","Desktop settings");

    /* 6: C:\USERS\GUEST\ */
    D("C:\\USERS\\GUEST");
    E( 6,"PROFILE.DAT", 0,   128,"07/27/26","DAT","Guest profile data");
    E( 6,"TEMP.TXT",    0,    64,"07/27/26","TXT","Temporary notes");

    /* 7: C:\USERS\SHARED\ */
    D("C:\\USERS\\SHARED");
    E( 7,"NOTES.TXT",   0,   512,"07/27/26","TXT","Shared notes file");
    E( 7,"README.TXT",  0,   243,"07/27/26","TXT","Shared readme");

    /* 8: C:\SYSTEM\ */
    D("C:\\SYSTEM");
    E( 8,"IDT.BIN",     0,  4096,"07/27/26","BIN","Interrupt descriptor table");
    E( 8,"GDT.BIN",     0,  2048,"07/27/26","BIN","Global descriptor table");
    E( 8,"ISR.BIN",     0,  8192,"07/27/26","BIN","ISR handler stubs");
    E( 8,"ATA.DRV",     0,  6144,"07/27/26","DRV","ATA/IDE drive driver");
    E( 8,"CDROM.DRV",   0,  5120,"07/27/26","DRV","ATAPI CD-ROM driver");
    E( 8,"PCI.DRV",     0,  4096,"07/27/26","DRV","PCI bus scanner");

    /* 9: C:\PACKAGES\ */
    D("C:\\PACKAGES");
    E( 9,"SAM.PKG",     0,131072,"07/27/26","PKG","SAM TTS engine package");
    E( 9,"BASIC.PKG",   0, 65536,"07/27/26","PKG","InteiliBASIC package");
    E( 9,"FONTS.PKG",   0,  8192,"07/27/26","PKG","VGA font package");

    /* 10: C:\TEMP\  (intentionally empty) */
    D("C:\\TEMP");

    /* 11: C:\LOGS\ */
    D("C:\\LOGS");
    E(11,"BOOT.LOG",    0,  2048,"07/27/26","LOG","Boot sequence log");
    E(11,"KERNEL.LOG",  0,  4096,"07/27/26","LOG","Kernel event log");
    E(11,"SHELL.LOG",   0,  1024,"07/27/26","LOG","Shell session log");
    E(11,"ERROR.LOG",   0,   512,"07/27/26","LOG","Error and fault log");

#undef D
#undef E
}

/* ── FS navigation helpers ────────────────────────────────────────────── */

static int fm_find_dir(const char *path) {
    for (int i = 0; i < fm_ndirs; i++)
        if (fm_strcmp(fm_dirs[i].path, path) == 0) return i;
    return -1;
}

static int fm_visible_count(int di) {
    int n = 0;
    for (int i = 0; i < fm_dirs[di].count; i++)
        if (!fm_dirs[di].e[i].deleted) n++;
    return n;
}

static fm_entry_t *fm_visible_entry(int di, int n) {
    int seen = 0;
    for (int i = 0; i < fm_dirs[di].count; i++) {
        if (!fm_dirs[di].e[i].deleted) {
            if (seen == n) return &fm_dirs[di].e[i];
            seen++;
        }
    }
    return 0;
}

/* "C:\USERS" + "ADMIN" -> "C:\USERS\ADMIN" */
static void fm_child_path(const char *parent, const char *name,
                          char *out, int max) {
    fm_strcpy(out, parent, max);
    int plen = fm_strlen(out);
    if (plen > 0 && out[plen-1] != '\\' && plen < max-1)
        { out[plen++] = '\\'; out[plen] = '\0'; }
    for (int i = 0; name[i] && plen+i < max-1; i++)
        out[plen+i] = name[i];
    out[plen + fm_strlen(name)] = '\0';
}

/* "C:\USERS\ADMIN" -> "C:\USERS",  "C:\BIN" -> "C:\" */
static void fm_parent_path(const char *path, char *out, int max) {
    fm_strcpy(out, path, max);
    int len = fm_strlen(out);
    /* strip trailing backslash */
    while (len > 3 && out[len-1] == '\\') { out[--len] = '\0'; }
    /* strip component name */
    while (len > 3 && out[len-1] != '\\') { out[--len] = '\0'; }
    /* ensure root ends with backslash */
    if (len <= 3) { out[2] = '\\'; out[3] = '\0'; }
    else if (out[len-1] == '\\') { out[--len] = '\0'; }
}

/* =========================================================================
 * Layout constants
 * ========================================================================= */
#define FM_TITLE_ROW   0
#define FM_PATH_ROW    1
#define FM_HDR_ROW     2
#define FM_DATA_ROW    3
#define FM_VIS_ROWS   18   /* rows 3–20 */
#define FM_SEP_ROW    21
#define FM_INFO_ROW   22
#define FM_HINT_ROW   23
#define FM_FOOT_ROW   24

/* Column starts */
#define COL_ICON    0
#define COL_NAME    1   /* 20 chars */
#define COL_SIZE   22   /*  8 chars */
#define COL_EXT    31   /*  5 chars */
#define COL_DATE   37   /*  8 chars */
#define COL_DESC   46   /* 34 chars */

/* =========================================================================
 * State
 * ========================================================================= */
#define FM_BROWSE   0
#define FM_MENU     1
#define FM_INFO     2
#define FM_RENAME   3
#define FM_CONFIRM  4

static int fm_state;
static int fm_cur_dir;
static int fm_cursor;
static int fm_scroll;
static int fm_menu_sel;

static char fm_ren_buf[FM_NLEN];
static int  fm_ren_len;

/* =========================================================================
 * Drawing — chrome
 * ========================================================================= */
static void fm_draw_title(void) {
    fm_fill(FM_TITLE_ROW, 0, ' ', VGA_COLOR_BLACK, VGA_COLOR_CYAN, 80);
    fm_puts(FM_TITLE_ROW, 2, "InteiliFile Manager 1.0",
            VGA_COLOR_BLACK, VGA_COLOR_CYAN, 23);
    fm_puts(FM_TITLE_ROW, 72, "Q=Quit  ",
            VGA_COLOR_BLACK, VGA_COLOR_CYAN, 8);
}

static void fm_draw_path(void) {
    fm_fill(FM_PATH_ROW, 0, ' ', VGA_COLOR_LIGHT_GREY, VGA_COLOR_DARK_GREY, 80);
    fm_puts(FM_PATH_ROW, 2, "Path: ", VGA_COLOR_LIGHT_GREY, VGA_COLOR_DARK_GREY, 6);
    fm_puts(FM_PATH_ROW, 8, fm_dirs[fm_cur_dir].path,
            VGA_COLOR_WHITE, VGA_COLOR_DARK_GREY, 70);
}

static void fm_draw_header(void) {
    fm_fill(FM_HDR_ROW, 0, ' ', VGA_COLOR_WHITE, VGA_COLOR_BLUE, 80);
    fm_puts(FM_HDR_ROW, COL_NAME, "Name",        VGA_COLOR_WHITE, VGA_COLOR_BLUE, 20);
    fm_puts(FM_HDR_ROW, COL_SIZE, "    Size",    VGA_COLOR_WHITE, VGA_COLOR_BLUE,  8);
    fm_puts(FM_HDR_ROW, COL_EXT,  " Type",       VGA_COLOR_WHITE, VGA_COLOR_BLUE,  6);
    fm_puts(FM_HDR_ROW, COL_DATE, "Date    ",    VGA_COLOR_WHITE, VGA_COLOR_BLUE,  8);
    fm_puts(FM_HDR_ROW, COL_DESC, "Description", VGA_COLOR_WHITE, VGA_COLOR_BLUE, 34);
}

static void fm_draw_separator(void) {
    fm_fill(FM_SEP_ROW, 0, BOX_H, VGA_COLOR_DARK_GREY, VGA_COLOR_BLACK, 80);
}

static void fm_draw_hints(void) {
    fm_fill(FM_HINT_ROW, 0, ' ', VGA_COLOR_WHITE, VGA_COLOR_DARK_GREY, 80);
    /* ▲▼ are CP437 0x1E / 0x1F */
    fm_puts(FM_HINT_ROW, 1,
            "\x1e\x1f Scroll   Enter=Open/Menu   Bksp=Up dir   Esc=Up/Quit   Q=Quit",
            VGA_COLOR_WHITE, VGA_COLOR_DARK_GREY, 78);
    fm_fill(FM_FOOT_ROW, 0, ' ', VGA_COLOR_DARK_GREY, VGA_COLOR_BLACK, 80);
    fm_puts(FM_FOOT_ROW, 2,
            "InteiliFile Manager 1.0  |  Inteilix Software Corporation",
            VGA_COLOR_DARK_GREY, VGA_COLOR_BLACK, 60);
}

/* =========================================================================
 * Drawing — entry row
 * ========================================================================= */
static void fm_draw_entry_row(int screen_row, fm_entry_t *e, int selected) {
    vga_color_t bg      = selected ? VGA_COLOR_BLUE  : VGA_COLOR_BLACK;
    vga_color_t fg_name = e->is_dir
        ? (selected ? VGA_COLOR_WHITE : VGA_COLOR_LIGHT_CYAN)
        : (selected ? VGA_COLOR_WHITE : VGA_COLOR_LIGHT_GREY);
    vga_color_t fg_meta = selected ? VGA_COLOR_LIGHT_GREY : VGA_COLOR_DARK_GREY;
    vga_color_t fg_desc = selected ? VGA_COLOR_LIGHT_GREY : VGA_COLOR_DARK_GREY;

    fm_fill(screen_row, 0, ' ', fg_name, bg, 80);

    /* Icon + name */
    /* 0x10 = ► (right-pointing triangle, CP437) for dirs */
    fm_poke(screen_row, COL_ICON,    e->is_dir ? '\x10' : ' ', fg_name, bg);
    fm_poke(screen_row, COL_ICON+1,  ' ', fg_name, bg);
    fm_puts(screen_row, COL_NAME, e->name, fg_name, bg, 19);

    /* Size */
    if (e->is_dir) {
        fm_puts(screen_row, COL_SIZE, "   <DIR>", fg_meta, bg, 8);
    } else {
        fm_put_uint(screen_row, COL_SIZE, e->size, 8, fg_meta, bg);
    }

    /* Space */
    fm_poke(screen_row, COL_SIZE+8, ' ', fg_meta, bg);

    /* Extension/type */
    fm_puts(screen_row, COL_EXT, e->ext, fg_meta, bg, 5);

    /* Space */
    fm_poke(screen_row, COL_EXT+5, ' ', fg_meta, bg);

    /* Date */
    fm_puts(screen_row, COL_DATE, e->date, fg_meta, bg, 8);

    /* Space */
    fm_poke(screen_row, COL_DATE+8, ' ', fg_meta, bg);

    /* Description */
    fm_puts(screen_row, COL_DESC, e->desc, fg_desc, bg, 34);
}

/* =========================================================================
 * Drawing — entry list + scrollbar
 * ========================================================================= */
static void fm_draw_entries(void) {
    int di  = fm_cur_dir;
    int vis = fm_visible_count(di);

    for (int r = FM_DATA_ROW; r < FM_DATA_ROW + FM_VIS_ROWS; r++)
        fm_fill(r, 0, ' ', VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK, 80);

    for (int i = fm_scroll; i < vis && i < fm_scroll + FM_VIS_ROWS; i++) {
        fm_entry_t *e = fm_visible_entry(di, i);
        if (!e) break;
        fm_draw_entry_row(FM_DATA_ROW + (i - fm_scroll), e, (i == fm_cursor));
    }

    /* Scrollbar on right edge when list exceeds viewport */
    if (vis > FM_VIS_ROWS) {
        int bar_top = FM_DATA_ROW + fm_scroll * FM_VIS_ROWS / vis;
        int bar_h   = FM_VIS_ROWS * FM_VIS_ROWS / vis;
        if (bar_h < 1) bar_h = 1;
        for (int r = FM_DATA_ROW; r < FM_DATA_ROW + FM_VIS_ROWS; r++) {
            int in_bar = (r >= bar_top && r < bar_top + bar_h);
            fm_poke(r, 79,
                    (unsigned char)(in_bar ? '\xDB' : '\xB0'),  /* █ / ░ */
                    in_bar ? VGA_COLOR_LIGHT_GREY : VGA_COLOR_DARK_GREY,
                    VGA_COLOR_BLACK);
        }
    }
}

/* =========================================================================
 * Drawing — info bar (row 22)
 * ========================================================================= */
static void fm_draw_info_bar(void) {
    int di  = fm_cur_dir;
    int vis = fm_visible_count(di);
    fm_fill(FM_INFO_ROW, 0, ' ', VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK, 80);

    if (vis == 0) {
        fm_puts(FM_INFO_ROW, 2, "(empty directory)",
                VGA_COLOR_DARK_GREY, VGA_COLOR_BLACK, 30);
        return;
    }

    fm_entry_t *e = fm_visible_entry(di, fm_cursor);
    if (!e) return;

    /* Left: name + size */
    char buf[60]; int bi = 0;
    buf[bi++] = ' ';
    for (int i = 0; e->name[i] && bi < 22; i++) buf[bi++] = e->name[i];
    if (e->is_dir) {
        buf[bi++]=' '; buf[bi++]='('; buf[bi++]='d'; buf[bi++]='i';
        buf[bi++]='r'; buf[bi++]=')';
    } else {
        buf[bi++]=' '; buf[bi++]=' ';
        char sz[12]; int sl = fm_uint_to_str(e->size, sz);
        for (int i=0;i<sl;i++) buf[bi++]=sz[i];
        buf[bi++]=' '; buf[bi++]='B';
    }
    buf[bi] = '\0';
    fm_puts(FM_INFO_ROW, 0, buf, VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK, bi);

    /* Right: item count */
    char cnt[16]; int ci = 0;
    char num[8]; int nl = fm_uint_to_str((uint32_t)vis, num);
    for (int i=0;i<nl;i++) cnt[ci++]=num[i];
    cnt[ci++]=' '; cnt[ci++]='i'; cnt[ci++]='t'; cnt[ci++]='e'; cnt[ci++]='m';
    if (vis != 1) cnt[ci++] = 's';
    cnt[ci] = '\0';
    int clen = fm_strlen(cnt);
    fm_puts(FM_INFO_ROW, 80 - clen - 1, cnt,
            VGA_COLOR_DARK_GREY, VGA_COLOR_BLACK, clen);
}

/* =========================================================================
 * Full redraw
 * ========================================================================= */
static void fm_redraw(void) {
    fm_draw_title();
    fm_draw_path();
    fm_draw_header();
    fm_draw_entries();
    fm_draw_separator();
    fm_draw_info_bar();
    fm_draw_hints();
}

/* =========================================================================
 * Popup — file action menu
 * ========================================================================= */
static const char * const fm_menu_items[] = {
    "  View Info        ",
    "  Open in IEdit    ",
    "  Copy             ",
    "  Rename           ",
    "  Delete           ",
    "  Cancel           ",
};
#define FM_MENU_N 6

/* Box: col 20, row 6, width 40, height 11 */
#define MPOP_ROW  6
#define MPOP_COL 20
#define MPOP_W   40
#define MPOP_H   11

static void fm_draw_menu(fm_entry_t *e, int sel) {
    fm_draw_box(MPOP_ROW, MPOP_COL, MPOP_H, MPOP_W,
                VGA_COLOR_WHITE, VGA_COLOR_BLUE);
    /* Title: filename */
    char hdr[MPOP_W]; int hi = 2;
    hdr[0]=' '; hdr[1]=' ';
    for (int i=0; e->name[i] && hi < MPOP_W-3; i++) hdr[hi++]=e->name[i];
    hdr[hi]='\0';
    fm_puts(MPOP_ROW+1, MPOP_COL+1, hdr, VGA_COLOR_LIGHT_CYAN, VGA_COLOR_BLUE, MPOP_W-2);

    fm_box_divider(MPOP_ROW+2, MPOP_COL, MPOP_W, VGA_COLOR_WHITE, VGA_COLOR_BLUE);

    /* File size/type info line */
    {
        char info[MPOP_W]; int ii=2;
        info[0]=' '; info[1]=' ';
        if (e->is_dir) {
            const char *d=" Type: Directory";
            while(*d && ii<MPOP_W-3) info[ii++]=*d++;
        } else {
            const char *t1=" Size: "; while(*t1 && ii<MPOP_W-3) info[ii++]=*t1++;
            char sb[12]; fm_uint_to_str(e->size, sb);
            for(int j=0;sb[j]&&ii<MPOP_W-3;j++) info[ii++]=sb[j];
            info[ii++]=' '; info[ii++]='B';
            const char *t2="   Type: "; while(*t2 && ii<MPOP_W-3) info[ii++]=*t2++;
            for(int j=0;e->ext[j]&&ii<MPOP_W-3;j++) info[ii++]=e->ext[j];
        }
        info[ii]='\0';
        fm_puts(MPOP_ROW+3, MPOP_COL+1, info,
                VGA_COLOR_DARK_GREY, VGA_COLOR_BLUE, MPOP_W-2);
    }

    fm_box_divider(MPOP_ROW+4, MPOP_COL, MPOP_W, VGA_COLOR_WHITE, VGA_COLOR_BLUE);

    /* Menu options */
    for (int i = 0; i < FM_MENU_N; i++) {
        vga_color_t fg = (i == sel) ? VGA_COLOR_BLACK     : VGA_COLOR_WHITE;
        vga_color_t bg = (i == sel) ? VGA_COLOR_LIGHT_CYAN : VGA_COLOR_BLUE;
        fm_puts(MPOP_ROW+5+i, MPOP_COL+1, fm_menu_items[i], fg, bg, MPOP_W-2);
    }
}

/* =========================================================================
 * Popup — file properties dialog
 * ========================================================================= */
#define INFO_ROW  4
#define INFO_COL 14
#define INFO_W   52
#define INFO_H   14

static void fm_draw_info_dialog(fm_entry_t *e) {
    fm_draw_box(INFO_ROW, INFO_COL, INFO_H, INFO_W,
                VGA_COLOR_WHITE, VGA_COLOR_BLUE);
    fm_puts(INFO_ROW+1, INFO_COL+2, "  File Properties",
            VGA_COLOR_LIGHT_CYAN, VGA_COLOR_BLUE, INFO_W-3);
    fm_box_divider(INFO_ROW+2, INFO_COL, INFO_W,
                   VGA_COLOR_WHITE, VGA_COLOR_BLUE);

    int r = INFO_ROW + 3;
    int vc = INFO_COL + 14;  /* value column */

#define PROP(lbl, val) do { \
    fm_puts(r, INFO_COL+2, (lbl), VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLUE, 12); \
    fm_puts(r++, vc, (val), VGA_COLOR_WHITE, VGA_COLOR_BLUE, INFO_W - 16); \
} while(0)

    PROP("Name     : ", e->name);
    PROP("Type     : ", e->is_dir ? "Directory" : e->ext);

    if (!e->is_dir) {
        char sb[16]; fm_uint_to_str(e->size, sb);
        /* append " bytes" */
        int sl = fm_strlen(sb);
        const char *sfx = " bytes";
        for (int i = 0; sfx[i] && sl < 14; i++) sb[sl++] = sfx[i];
        sb[sl] = '\0';
        PROP("Size     : ", sb);
    } else {
        r++;
    }

    PROP("Date     : ", e->date);
    PROP("Location : ", fm_dirs[fm_cur_dir].path);
    PROP("Desc     : ", e->desc);

#undef PROP

    fm_box_divider(r++, INFO_COL, INFO_W, VGA_COLOR_WHITE, VGA_COLOR_BLUE);
    fm_puts(r, INFO_COL+2, "  Press any key to close",
            VGA_COLOR_DARK_GREY, VGA_COLOR_BLUE, INFO_W-4);
}

/* =========================================================================
 * Popup — rename dialog
 * ========================================================================= */
#define REN_ROW  9
#define REN_COL 20
#define REN_W   40
#define REN_H    7

static void fm_draw_rename(fm_entry_t *e) {
    fm_draw_box(REN_ROW, REN_COL, REN_H, REN_W,
                VGA_COLOR_WHITE, VGA_COLOR_BLUE);
    fm_puts(REN_ROW+1, REN_COL+2, "  Rename File",
            VGA_COLOR_LIGHT_CYAN, VGA_COLOR_BLUE, REN_W-3);
    fm_box_divider(REN_ROW+2, REN_COL, REN_W, VGA_COLOR_WHITE, VGA_COLOR_BLUE);
    fm_puts(REN_ROW+3, REN_COL+2, "Old: ",
            VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLUE, 5);
    fm_puts(REN_ROW+3, REN_COL+7, e->name,
            VGA_COLOR_WHITE, VGA_COLOR_BLUE, REN_W-8);
    fm_puts(REN_ROW+4, REN_COL+2, "New: ",
            VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLUE, 5);
    /* Input field */
    fm_fill(REN_ROW+4, REN_COL+7, ' ', VGA_COLOR_BLACK, VGA_COLOR_LIGHT_GREY, REN_W-9);
    if (fm_ren_len > 0)
        fm_puts(REN_ROW+4, REN_COL+7, fm_ren_buf,
                VGA_COLOR_BLACK, VGA_COLOR_LIGHT_GREY, fm_ren_len);
    /* Blinking-style cursor */
    fm_poke(REN_ROW+4, REN_COL+7+fm_ren_len, '_',
            VGA_COLOR_BLACK, VGA_COLOR_WHITE);
    fm_puts(REN_ROW+5, REN_COL+2, "Enter=Confirm   Esc=Cancel",
            VGA_COLOR_DARK_GREY, VGA_COLOR_BLUE, REN_W-4);
}

/* =========================================================================
 * Popup — delete confirm
 * ========================================================================= */
#define DEL_ROW 10
#define DEL_COL 20
#define DEL_W   40
#define DEL_H    6

static void fm_draw_confirm(fm_entry_t *e) {
    fm_draw_box(DEL_ROW, DEL_COL, DEL_H, DEL_W,
                VGA_COLOR_LIGHT_RED, VGA_COLOR_BLUE);
    fm_puts(DEL_ROW+1, DEL_COL+2, "  Delete File?",
            VGA_COLOR_LIGHT_RED, VGA_COLOR_BLUE, DEL_W-3);
    fm_box_divider(DEL_ROW+2, DEL_COL, DEL_W, VGA_COLOR_LIGHT_RED, VGA_COLOR_BLUE);
    fm_puts(DEL_ROW+3, DEL_COL+3, e->name,
            VGA_COLOR_WHITE, VGA_COLOR_BLUE, DEL_W-4);
    fm_puts(DEL_ROW+4, DEL_COL+2, "Enter=Delete   Esc=Cancel",
            VGA_COLOR_DARK_GREY, VGA_COLOR_BLUE, DEL_W-4);
}

/* =========================================================================
 * Navigation helpers
 * ========================================================================= */
static void fm_clamp(void) {
    int vis = fm_visible_count(fm_cur_dir);
    if (vis == 0) { fm_cursor = 0; fm_scroll = 0; return; }
    if (fm_cursor < 0)    fm_cursor = 0;
    if (fm_cursor >= vis) fm_cursor = vis - 1;
    if (fm_cursor < fm_scroll)
        fm_scroll = fm_cursor;
    if (fm_cursor >= fm_scroll + FM_VIS_ROWS)
        fm_scroll = fm_cursor - FM_VIS_ROWS + 1;
}

static void fm_enter(int new_di) {
    fm_cur_dir = new_di;
    fm_cursor  = 0;
    fm_scroll  = 0;
}

static int fm_go_up(void) {
    if (fm_strcmp(fm_dirs[fm_cur_dir].path, "C:\\") == 0)
        return 0;   /* already at root */
    char ppath[FM_PLEN];
    fm_parent_path(fm_dirs[fm_cur_dir].path, ppath, FM_PLEN);
    int pdi = fm_find_dir(ppath);
    if (pdi >= 0) { fm_enter(pdi); return 1; }
    return 0;
}

/* =========================================================================
 * Generic notice popup (opens, waits for any key, returns)
 * ========================================================================= */
static void fm_notice(const char *title, const char *body) {
    int nr = 10, nc = 16, nw = 48, nh = 6;
    fm_draw_box(nr, nc, nh, nw, VGA_COLOR_WHITE, VGA_COLOR_BLUE);
    fm_puts(nr+1, nc+2, title, VGA_COLOR_LIGHT_CYAN, VGA_COLOR_BLUE, nw-3);
    fm_box_divider(nr+2, nc, nw, VGA_COLOR_WHITE, VGA_COLOR_BLUE);
    fm_puts(nr+3, nc+2, body, VGA_COLOR_WHITE, VGA_COLOR_BLUE, nw-4);
    fm_puts(nr+4, nc+2, "Press any key to continue.",
            VGA_COLOR_DARK_GREY, VGA_COLOR_BLUE, nw-4);
    keyboard_getchar();
}

/* =========================================================================
 * Main entry point
 * ========================================================================= */
void filemanager_run(void) {
    fm_init_fs();
    fm_state    = FM_BROWSE;
    fm_cur_dir  = 0;
    fm_cursor   = 0;
    fm_scroll   = 0;
    fm_menu_sel = 0;

    /* Blank the screen */
    for (int r = 0; r < 25; r++)
        fm_fill(r, 0, ' ', VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK, 80);

    fm_redraw();
    vga_set_cursor(25, 0);   /* park cursor off-screen */

    while (1) {
        int ch = keyboard_getchar();

        int  di  = fm_cur_dir;
        int  vis = fm_visible_count(di);
        fm_entry_t *ce = (vis > 0) ? fm_visible_entry(di, fm_cursor) : 0;

        /* ── BROWSE ──────────────────────────────────────────────────────── */
        if (fm_state == FM_BROWSE) {

            if (ch == 'q' || ch == 'Q') break;

            if (ch == KEY_UP) {
                if (fm_cursor > 0) fm_cursor--;
                fm_clamp();
                fm_draw_entries();
                fm_draw_info_bar();

            } else if (ch == KEY_DOWN) {
                if (fm_cursor < vis - 1) fm_cursor++;
                fm_clamp();
                fm_draw_entries();
                fm_draw_info_bar();

            } else if (ch == KEY_ENTER) {
                if (!ce) continue;
                if (ce->is_dir) {
                    /* Navigate into subdirectory */
                    char cpath[FM_PLEN];
                    fm_child_path(fm_dirs[di].path, ce->name, cpath, FM_PLEN);
                    int ndi = fm_find_dir(cpath);
                    if (ndi >= 0) {
                        fm_enter(ndi);
                        fm_redraw();
                        vga_set_cursor(25, 0);
                    } else {
                        fm_notice("Cannot Open", "Subdirectory not indexed.");
                        fm_redraw();
                        vga_set_cursor(25, 0);
                    }
                } else {
                    /* Open file action menu */
                    fm_state    = FM_MENU;
                    fm_menu_sel = 0;
                    fm_redraw();
                    fm_draw_menu(ce, fm_menu_sel);
                    vga_set_cursor(25, 0);
                }

            } else if (ch == KEY_BACKSPACE || ch == KEY_LEFT) {
                if (fm_go_up()) {
                    fm_redraw();
                    vga_set_cursor(25, 0);
                }

            } else if (ch == KEY_ESCAPE) {
                /* At root: quit; otherwise go up */
                if (!fm_go_up()) break;
                fm_redraw();
                vga_set_cursor(25, 0);
            }

        /* ── MENU ────────────────────────────────────────────────────────── */
        } else if (fm_state == FM_MENU) {
            if (!ce) { fm_state = FM_BROWSE; fm_redraw(); continue; }

            if (ch == KEY_UP) {
                if (fm_menu_sel > 0) fm_menu_sel--;
                fm_draw_menu(ce, fm_menu_sel);

            } else if (ch == KEY_DOWN) {
                if (fm_menu_sel < FM_MENU_N - 1) fm_menu_sel++;
                fm_draw_menu(ce, fm_menu_sel);

            } else if (ch == KEY_ESCAPE || ch == 'q' || ch == 'Q') {
                fm_state = FM_BROWSE;
                fm_redraw();

            } else if (ch == KEY_ENTER) {
                switch (fm_menu_sel) {

                case 0: /* View Info */
                    fm_state = FM_INFO;
                    fm_redraw();
                    fm_draw_info_dialog(ce);
                    break;

                case 1: /* Open in IEdit */
                    fm_state = FM_BROWSE;
                    fm_redraw();
                    fm_notice("Open in IEdit",
                              "IFS driver required for file I/O.");
                    fm_redraw();
                    break;

                case 2: /* Copy */
                    fm_state = FM_BROWSE;
                    fm_redraw();
                    fm_notice("Copy File",
                              "No destination specified.");
                    fm_redraw();
                    break;

                case 3: /* Rename */
                    fm_state   = FM_RENAME;
                    fm_ren_len = 0;
                    fm_ren_buf[0] = '\0';
                    fm_redraw();
                    fm_draw_rename(ce);
                    break;

                case 4: /* Delete */
                    fm_state = FM_CONFIRM;
                    fm_redraw();
                    fm_draw_confirm(ce);
                    break;

                case 5: /* Cancel */
                    fm_state = FM_BROWSE;
                    fm_redraw();
                    break;
                }

            } else {
                /* 1–6 number shortcuts */
                int s = ch - '1';
                if (s >= 0 && s < FM_MENU_N) {
                    fm_menu_sel = s;
                    fm_draw_menu(ce, fm_menu_sel);
                }
            }

        /* ── INFO ────────────────────────────────────────────────────────── */
        } else if (fm_state == FM_INFO) {
            fm_state = FM_BROWSE;
            fm_redraw();

        /* ── RENAME ──────────────────────────────────────────────────────── */
        } else if (fm_state == FM_RENAME) {
            if (!ce) { fm_state = FM_BROWSE; fm_redraw(); continue; }

            if (ch == KEY_ESCAPE) {
                fm_state = FM_MENU;
                fm_redraw();
                fm_draw_menu(ce, fm_menu_sel);

            } else if (ch == KEY_ENTER) {
                if (fm_ren_len > 0) {
                    fm_ren_buf[fm_ren_len] = '\0';
                    fm_strcpy(ce->name, fm_ren_buf, FM_NLEN);
                }
                fm_state = FM_BROWSE;
                fm_redraw();

            } else if (ch == KEY_BACKSPACE) {
                if (fm_ren_len > 0) {
                    fm_ren_buf[--fm_ren_len] = '\0';
                    fm_draw_rename(ce);
                }

            } else if (ch >= 0x20 && ch < 0x7F && fm_ren_len < FM_NLEN - 1) {
                char c = (char)ch;
                if (c >= 'a' && c <= 'z') c -= 32;   /* force upper-case */
                fm_ren_buf[fm_ren_len++] = c;
                fm_ren_buf[fm_ren_len]   = '\0';
                fm_draw_rename(ce);
            }

        /* ── CONFIRM (delete) ────────────────────────────────────────────── */
        } else if (fm_state == FM_CONFIRM) {
            if (ch == KEY_ENTER || ch == 'y' || ch == 'Y') {
                if (ce) ce->deleted = 1;
                fm_clamp();
                fm_state = FM_BROWSE;
                fm_redraw();

            } else if (ch == KEY_ESCAPE || ch == 'n' || ch == 'N') {
                fm_state = FM_MENU;
                fm_redraw();
                /* re-fetch ce (deletion did not happen) */
                ce = fm_visible_entry(di, fm_cursor);
                if (ce) fm_draw_menu(ce, fm_menu_sel);
            }
        }
    }

    vga_clear();
    vga_set_color(VGA_COLOR_WHITE, VGA_COLOR_BLACK);
}
