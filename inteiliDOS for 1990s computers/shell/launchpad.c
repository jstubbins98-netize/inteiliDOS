/*
 * inteiliDOS -- shell/launchpad.c
 * LaunchPad 1.0  —  Program Manager
 *
 * Reads real filesystems from attached hardware:
 *   CD-ROM sources  — ISO 9660 root directory via kernel/iso9660.c
 *   Floppy sources  — FAT12 root directory via kernel/fat12.c
 *
 * Programs are loaded into RAM at IPGM_LOAD_ADDR (0x00500000) and
 * executed via kernel/loader.c.  Programs must begin with a 16-byte
 * IPGM header (see loader.h).
 *
 * Screen layout (80×25 VGA text mode):
 *   Row  0    Title bar                  (black on cyan)
 *   Row  1    Source selector tabs       (white on dark-grey / white on blue)
 *   Row  2    Column headers             (white on blue)
 *   Rows 3-20 18 scrollable file entries
 *   Row 21    Separator
 *   Row 22    Selected-item info line
 *   Row 23    Key-binding hints
 *   Row 24    Footer                     (black on cyan)
 *
 * Keys:
 *   Up / Down   — navigate
 *   F2 (0x91)   — cycle source
 *   F3 (0x92)   — rescan current source for media changes
 *   F5 (0x94)   — eject current CD-ROM
 *   Enter       — load (and run) selected program
 *   Esc / Q     — exit
 *
 * Hot-media detection:
 *   LaunchPad keeps its own mutable copy of the drive table.  On every
 *   source switch (F2), manual rescan (F3), or eject (F5) it calls
 *   cdrom_rescan_media() / fdc_init() to re-probe actual hardware state
 *   before listing the directory.  This lets a disc inserted after boot
 *   be detected without rebooting.
 */

#include "launchpad.h"
#include "../kernel/iso9660.h"
#include "../kernel/fat12.h"
#include "../kernel/loader.h"
#include "../kernel/cdrom.h"
#include "../kernel/fdc.h"
#include "../kernel/vga.h"
#include "../kernel/keyboard.h"
#include <stdint.h>
#include <stddef.h>

/* =========================================================================
 * VGA direct-write helpers
 * ========================================================================= */
static volatile uint16_t *const LP_VGA = (volatile uint16_t *)0xB8000U;

static inline void lp_poke(int row, int col, unsigned char c,
                            vga_color_t fg, vga_color_t bg) {
    if ((unsigned)row >= 25 || (unsigned)col >= 80) return;
    LP_VGA[row * 80 + col] =
        (uint16_t)(((uint8_t)bg << 12) | ((uint8_t)fg << 8) | c);
}
static void lp_fill(int row, int col, unsigned char c,
                    vga_color_t fg, vga_color_t bg, int n) {
    for (int i = 0; i < n; i++) lp_poke(row, col + i, c, fg, bg);
}
static void lp_puts(int row, int col, const char *s,
                    vga_color_t fg, vga_color_t bg, int max_w) {
    int i = 0;
    for (; s && s[i] && i < max_w; i++)
        lp_poke(row, col + i, (unsigned char)s[i], fg, bg);
    for (; i < max_w; i++)
        lp_poke(row, col + i, ' ', fg, bg);
}
static void lp_put_uint_r(int row, int col, uint32_t v, int w,
                           vga_color_t fg, vga_color_t bg) {
    char buf[12]; int n = 0;
    if (!v) buf[n++] = '0';
    else { uint32_t x = v; while (x) { buf[n++] = (char)('0' + x % 10); x /= 10; } }
    int pad = w - n;
    for (int p = 0; p < pad; p++) lp_poke(row, col++, ' ', fg, bg);
    for (int d = n - 1; d >= 0; d--) lp_poke(row, col++, (unsigned char)buf[d], fg, bg);
}

/* =========================================================================
 * Screen and key constants
 * ========================================================================= */
#define LP_F2  ((int)(KEY_F1) + 1)
#define LP_F3  ((int)(KEY_F1) + 2)
#define LP_F5  ((int)(KEY_F1) + 4)

#define LP_SRC_CDROM0   0
#define LP_SRC_CDROM1   1
#define LP_SRC_CDROM2   2
#define LP_SRC_CDROM3   3
#define LP_SRC_FLOPPY_A 4
#define LP_SRC_COUNT    5

static const char *lp_src_label[LP_SRC_COUNT] = {
    "CD-ROM 0", "CD-ROM 1", "CD-ROM 2", "CD-ROM 3", "Floppy A:"
};
static int lp_src_is_cdrom(int src) { return src <= LP_SRC_CDROM3; }

/* =========================================================================
 * Real file listing
 * ========================================================================= */

#define LP_MAX_FILES   64

typedef struct {
    char     name[ISO9660_NAME_MAX];  /* filename (up to 31 chars + NUL)  */
    uint32_t size;                    /* file size in bytes                */
    uint8_t  is_dir;                  /* 1 = directory                     */
    /* Cached addressing info for fast reload */
    uint32_t cd_lba;                  /* ISO 9660: start LBA on disc       */
    uint16_t fat_cluster;             /* FAT12: first cluster              */
} lp_file_t;

static lp_file_t lp_files[LP_MAX_FILES];
static int       lp_file_count = 0;
static int       lp_listing_ok = 0;
static char      lp_err[48]    = "";  /* error message when listing fails */

/* =========================================================================
 * Directory navigation stack
 * =========================================================================
 * lp_depth == 0  → currently showing the root directory
 * lp_depth >  0  → currently inside a subdirectory; lp_dirstack[lp_depth-1]
 *                  describes the current directory.
 *
 * lp_dirstack entries carry whichever addressing info applies to the source:
 *   cd_lba / cd_size  — ISO 9660: sector address and byte length of this dir
 *   fat_cluster       — FAT12:    first cluster of this directory (>= 2)
 */
#define LP_DIR_DEPTH  8

typedef struct {
    char     name[ISO9660_NAME_MAX]; /* dir name for breadcrumb display  */
    uint32_t cd_lba;                 /* ISO 9660: LBA of directory       */
    uint32_t cd_size;                /* ISO 9660: byte length of dir     */
    uint16_t fat_cluster;            /* FAT12: first cluster (>= 2)      */
} lp_dir_t;

static lp_dir_t lp_dirstack[LP_DIR_DEPTH];
static int      lp_depth = 0;

/* Lazy FDC init: only spin up the floppy controller on first use. */
static int lp_fdc_ready = 0;

static int lp_ensure_fdc(void) {
    if (lp_fdc_ready) return 0;
    if (fdc_init() != 0) return -1;
    lp_fdc_ready = 1;
    return 0;
}

/*
 * lp_probe_source — re-probe hardware for the given source and update the
 * local drives[] copy.  Called on F2 (source switch), F3 (manual rescan),
 * and F5 (after eject) so that discs inserted or removed after boot are
 * detected without a reboot.
 *
 *   src    : LP_SRC_CDROM0..LP_SRC_CDROM3 or LP_SRC_FLOPPY_A
 *   drives : caller's local mutable copy of the drive table
 */
static void lp_probe_source(int src, cdrom_drive_t *drives) {
    if (lp_src_is_cdrom(src)) {
        uint8_t di = (uint8_t)src;
        if (di < CDROM_MAX_DRIVES && drives[di].present == CDROM_PRESENT) {
            /* Re-issue READ CAPACITY to detect inserted / removed discs. */
            cdrom_rescan_media(di);
            /* Sync our local copy from the updated internal table. */
            const cdrom_drive_t *tbl = cdrom_drives();
            drives[di] = tbl[di];
        }
    } else {
        /*
         * Floppy: force a fresh FDC init + recalibrate so that a disk
         * inserted after boot is seen.  Clearing lp_fdc_ready forces
         * lp_ensure_fdc() to call fdc_init() again next time it is needed.
         */
        lp_fdc_ready = 0;
        /* Eagerly try to init now; ignore failure (no disk = init fails). */
        lp_ensure_fdc();
    }
}

/* Copy a C string (no stdlib). */
static void lp_strcpy(char *dst, const char *src, int max) {
    int i = 0;
    for (; src[i] && i < max - 1; i++) dst[i] = src[i];
    dst[i] = '\0';
}
static int lp_strlen(const char *s) {
    int n = 0; while (s[n]) n++; return n;
}

/*
 * lp_build_path — write the current breadcrumb path into buf.
 * Root produces "/", one level deep produces "/DIRNAME", etc.
 * If the full path would exceed max, it is left-truncated with "..".
 */
static void lp_build_path(char *buf, int max) {
    if (max <= 1) { buf[0] = '\0'; return; }
    /* Build into a scratch buffer first so we can truncate from the left. */
    char tmp[LP_DIR_DEPTH * (ISO9660_NAME_MAX + 1) + 2];
    int pos = 0;
    tmp[pos++] = '/';
    for (int i = 0; i < lp_depth; i++) {
        if (i > 0) tmp[pos++] = '/';
        for (int j = 0; lp_dirstack[i].name[j]; j++)
            tmp[pos++] = lp_dirstack[i].name[j];
    }
    tmp[pos] = '\0';
    /* Truncate from left if needed, with leading ".." marker. */
    if (pos < max) {
        lp_strcpy(buf, tmp, max);
    } else {
        /* Take the rightmost (max-3) characters, prefix "..". */
        buf[0] = '.'; buf[1] = '.';
        lp_strcpy(buf + 2, tmp + pos - (max - 3), max - 2);
    }
}

/* Populate lp_files[] from the selected source. */
static void lp_refresh_listing(int src, const cdrom_drive_t *drives) {
    lp_file_count = 0;
    lp_listing_ok = 0;
    lp_err[0]     = '\0';

    if (lp_src_is_cdrom(src)) {
        uint8_t di = (uint8_t)src;
        /* Verify drive is present and has a disc. */
        if (di >= CDROM_MAX_DRIVES || drives[di].present != CDROM_PRESENT) {
            lp_strcpy(lp_err, "No CD-ROM drive at this position.", 48);
            return;
        }
        if (drives[di].last_lba == 0) {
            lp_strcpy(lp_err, "No disc inserted.  Insert a CD and press F2.", 48);
            return;
        }
        iso9660_dirent_t iso_ents[ISO9660_MAX_FILES];
        int n;
        if (lp_depth == 0) {
            n = iso9660_read_dir(di, iso_ents);
        } else {
            const lp_dir_t *cur = &lp_dirstack[lp_depth - 1];
            n = iso9660_read_dir_at(di, cur->cd_lba, cur->cd_size, iso_ents);
        }
        if (n < 0) {
            lp_strcpy(lp_err, "Cannot read disc (not ISO 9660 / unreadable).", 48);
            return;
        }
        for (int i = 0; i < n && i < LP_MAX_FILES; i++) {
            lp_strcpy(lp_files[i].name, iso_ents[i].name, ISO9660_NAME_MAX);
            lp_files[i].size        = iso_ents[i].size;
            lp_files[i].is_dir      = iso_ents[i].is_dir;
            lp_files[i].cd_lba      = iso_ents[i].lba;
            lp_files[i].fat_cluster = 0;
        }
        lp_file_count = n < LP_MAX_FILES ? n : LP_MAX_FILES;
        lp_listing_ok = 1;

    } else {
        /* Floppy source */
        if (lp_ensure_fdc() != 0) {
            lp_strcpy(lp_err, "Floppy controller not responding.", 48);
            return;
        }
        uint8_t flop = (uint8_t)(src - LP_SRC_FLOPPY_A);
        fat12_dirent_t fat_ents[FAT12_MAX_FILES];
        int n;
        if (lp_depth == 0) {
            n = fat12_read_dir(flop, fat_ents);
        } else {
            const lp_dir_t *cur = &lp_dirstack[lp_depth - 1];
            n = fat12_read_subdir(flop, cur->fat_cluster, fat_ents);
        }
        if (n < 0) {
            lp_strcpy(lp_err, "Cannot read floppy (no disk or not FAT12).", 48);
            return;
        }
        for (int i = 0; i < n && i < LP_MAX_FILES; i++) {
            lp_strcpy(lp_files[i].name, fat_ents[i].name, ISO9660_NAME_MAX);
            lp_files[i].size        = fat_ents[i].size;
            lp_files[i].is_dir      = (fat_ents[i].attr & FAT12_ATTR_DIRECTORY)
                                       ? 1u : 0u;
            lp_files[i].cd_lba      = 0;
            lp_files[i].fat_cluster = fat_ents[i].first_cluster;
        }
        lp_file_count = n < LP_MAX_FILES ? n : LP_MAX_FILES;
        lp_listing_ok = 1;
    }
}

/* =========================================================================
 * File type helpers (derived from extension)
 * ========================================================================= */

/* Return the extension part of a filename, or "" if none. */
static const char *lp_ext(const char *name) {
    const char *dot = (const char *)0;
    for (int i = 0; name[i]; i++)
        if (name[i] == '.') dot = name + i;
    return dot ? dot + 1 : "";
}

/* Case-insensitive 3-char extension compare. */
static int lp_ext_eq(const char *ext, const char *cmp) {
    for (int i = 0; i < 3; i++) {
        int a = (unsigned char)ext[i];
        int b = (unsigned char)cmp[i];
        if (a >= 'a' && a <= 'z') a -= 32;
        if (b >= 'a' && b <= 'z') b -= 32;
        if (a != b) return 0;
        if (!a)     return 1;
    }
    return 1;
}

static const char *lp_ext_type(const char *name) {
    const char *e = lp_ext(name);
    if (!e[0])              return "File";
    if (lp_ext_eq(e,"IPGM")) return "Program";
    if (lp_ext_eq(e,"ELF"))  return "Program";
    if (lp_ext_eq(e,"COM"))  return "Program";
    if (lp_ext_eq(e,"EXE"))  return "Program";
    if (lp_ext_eq(e,"BAS"))  return "BASIC";
    if (lp_ext_eq(e,"TXT"))  return "Text";
    if (lp_ext_eq(e,"SHT"))  return "Sheet";
    if (lp_ext_eq(e,"BMP"))  return "Image";
    if (lp_ext_eq(e,"WAV"))  return "Audio";
    if (lp_ext_eq(e,"ISO"))  return "ISO Image";
    return "File";
}

static const char *lp_ext_info(const char *name, uint8_t is_dir) {
    if (is_dir) return "Directory -- navigate with Enter";
    const char *e = lp_ext(name);
    if (!e[0])               return "";
    if (lp_ext_eq(e,"IPGM")) return "inteiliDOS executable -- press Enter to run";
    if (lp_ext_eq(e,"ELF"))  return "ELF executable -- press Enter to run";
    if (lp_ext_eq(e,"COM"))  return "Program -- press Enter to run";
    if (lp_ext_eq(e,"EXE"))  return "Program -- press Enter to run";
    if (lp_ext_eq(e,"BAS"))  return "InteiliBASIC source -- load with BASIC";
    if (lp_ext_eq(e,"TXT"))  return "Text file -- view with READER or IEDIT";
    if (lp_ext_eq(e,"WAV"))  return "Audio file -- play with TALK";
    return "";
}

/* =========================================================================
 * Screen drawing
 * ========================================================================= */

static void lp_draw_title(void) {
    lp_fill(0, 0, ' ', VGA_COLOR_BLACK, VGA_COLOR_CYAN, 80);
    lp_puts(0, 2, "inteiliDOS LaunchPad 1.0",
            VGA_COLOR_BLACK, VGA_COLOR_CYAN, 26);
    /* Right side: show current directory path, or static label at root. */
    if (lp_depth == 0) {
        lp_puts(0, 52, "Load & Run inteiliDOS Programs",
                VGA_COLOR_BLACK, VGA_COLOR_CYAN, 28);
    } else {
        char path[30];
        lp_build_path(path, 30);
        lp_puts(0, 49, path, VGA_COLOR_DARK_GREY, VGA_COLOR_CYAN, 31);
    }
}

static void lp_draw_source_bar(int src, const cdrom_drive_t *drives) {
    lp_fill(1, 0, ' ', VGA_COLOR_WHITE, VGA_COLOR_DARK_GREY, 80);
    lp_puts(1, 1, "Source:", VGA_COLOR_LIGHT_GREY, VGA_COLOR_DARK_GREY, 7);

    int col = 9;
    for (int i = 0; i < LP_SRC_COUNT && col < 72; i++) {
        vga_color_t fg = (i == src) ? VGA_COLOR_BLACK       : VGA_COLOR_LIGHT_CYAN;
        vga_color_t bg = (i == src) ? VGA_COLOR_LIGHT_CYAN  : VGA_COLOR_DARK_GREY;

        /* Status indicator for CD-ROM: * = no drive, - = no disc */
        char status = 0;
        if (i <= LP_SRC_CDROM3) {
            uint8_t di = (uint8_t)i;
            if (di >= CDROM_MAX_DRIVES || drives[di].present != CDROM_PRESENT)
                status = '*';
            else if (drives[di].last_lba == 0)
                status = '-';
        }

        lp_poke(1, col++, ' ', fg, bg);
        for (int j = 0; lp_src_label[i][j] && col < 79; j++, col++)
            lp_poke(1, col, (unsigned char)lp_src_label[i][j], fg, bg);
        if (status) lp_poke(1, col++, (unsigned char)status, fg, bg);
        lp_poke(1, col++, ' ', fg, bg);
        if (col < 79) lp_poke(1, col++, ' ', VGA_COLOR_DARK_GREY, VGA_COLOR_DARK_GREY);
    }
}

static void lp_draw_header(void) {
    lp_fill(2, 0, ' ', VGA_COLOR_WHITE, VGA_COLOR_BLUE, 80);
    lp_puts(2,  2, "Name",   VGA_COLOR_WHITE, VGA_COLOR_BLUE, 14);
    lp_puts(2, 16, "   Size", VGA_COLOR_WHITE, VGA_COLOR_BLUE,  9);
    lp_puts(2, 25, "Type",   VGA_COLOR_WHITE, VGA_COLOR_BLUE, 10);
    lp_puts(2, 35, "Info",   VGA_COLOR_WHITE, VGA_COLOR_BLUE, 45);
}

static void lp_draw_list(int sel, int top) {
    for (int row = 0; row < 18; row++) {
        int  idx  = top + row;
        int  vrow = 3 + row;

        if (!lp_listing_ok || idx >= lp_file_count) {
            lp_fill(vrow, 0, ' ', VGA_COLOR_WHITE, VGA_COLOR_BLACK, 80);
            if (!lp_listing_ok && row == 2) {
                /* Show error message centred in the empty list area. */
                int elen = lp_strlen(lp_err);
                int ecol = (80 - elen) / 2;
                if (ecol < 2) ecol = 2;
                lp_puts(vrow, ecol, lp_err,
                        VGA_COLOR_LIGHT_RED, VGA_COLOR_BLACK, 76);
            }
            continue;
        }

        int is_sel = (idx == sel);
        vga_color_t fg  = is_sel ? VGA_COLOR_BLACK      : VGA_COLOR_LIGHT_CYAN;
        vga_color_t bg  = is_sel ? VGA_COLOR_LIGHT_CYAN : VGA_COLOR_BLACK;
        vga_color_t sfg = is_sel ? VGA_COLOR_BLACK      : VGA_COLOR_LIGHT_GREEN;
        vga_color_t dfg = is_sel ? VGA_COLOR_BLACK      : VGA_COLOR_DARK_GREY;

        lp_file_t *f = &lp_files[idx];

        lp_fill(vrow, 0, ' ', fg, bg, 80);

        /* Directory indicator or name */
        if (f->is_dir) {
            lp_poke(vrow, 2, '[', VGA_COLOR_LIGHT_BROWN, bg);
            lp_puts(vrow, 3, f->name, VGA_COLOR_LIGHT_BROWN, bg, 11);
            lp_poke(vrow, 14, ']', VGA_COLOR_LIGHT_BROWN, bg);
        } else {
            lp_puts(vrow, 2, f->name, fg, bg, 14);
        }

        /* Size */
        if (f->is_dir) {
            lp_puts(vrow, 16, "    <DIR>", dfg, bg, 9);
        } else if (f->size >= 1024u) {
            lp_put_uint_r(vrow, 16, f->size / 1024u, 6, sfg, bg);
            lp_puts(vrow, 22, " KB", sfg, bg, 3);
        } else {
            lp_put_uint_r(vrow, 16, f->size, 6, sfg, bg);
            lp_puts(vrow, 22, "  B", sfg, bg, 3);
        }

        /* Type */
        lp_puts(vrow, 25, lp_ext_type(f->name), fg, bg, 10);

        /* Info */
        lp_puts(vrow, 35, lp_ext_info(f->name, f->is_dir), dfg, bg, 45);
    }

    /* Scroll indicators */
    if (top > 0)
        lp_poke(3,  79, '^', VGA_COLOR_LIGHT_BROWN, VGA_COLOR_BLACK);
    if (lp_listing_ok && top + 18 < lp_file_count)
        lp_poke(20, 79, 'v', VGA_COLOR_LIGHT_BROWN, VGA_COLOR_BLACK);
}

static void lp_draw_sep(void) {
    lp_fill(21, 0, '-', VGA_COLOR_DARK_GREY, VGA_COLOR_BLACK, 80);
}

static void lp_draw_desc(int sel) {
    lp_fill(22, 0, ' ', VGA_COLOR_WHITE, VGA_COLOR_BLACK, 80);
    if (!lp_listing_ok || sel < 0 || sel >= lp_file_count) return;
    lp_file_t *f = &lp_files[sel];
    lp_puts(22, 1, f->name, VGA_COLOR_LIGHT_BROWN, VGA_COLOR_BLACK, 22);
    if (f->size) {
        lp_puts(22, 24, "  Size:", VGA_COLOR_DARK_GREY, VGA_COLOR_BLACK, 7);
        if (f->size >= 1024u) {
            lp_put_uint_r(22, 31, f->size / 1024u, 6, VGA_COLOR_WHITE, VGA_COLOR_BLACK);
            lp_puts(22, 37, " KB", VGA_COLOR_WHITE, VGA_COLOR_BLACK, 3);
        } else {
            lp_put_uint_r(22, 31, f->size, 6, VGA_COLOR_WHITE, VGA_COLOR_BLACK);
            lp_puts(22, 37, "  B", VGA_COLOR_WHITE, VGA_COLOR_BLACK, 3);
        }
    }
}

static void lp_draw_hints(void) {
    lp_fill(23, 0, ' ', VGA_COLOR_DARK_GREY, VGA_COLOR_BLACK, 80);
    if (lp_depth == 0) {
        lp_puts(23, 1,
            "Up/Dn=Select  Enter=Open  F2=Source  F3=Rescan  F5=Eject  Esc=Exit",
            VGA_COLOR_DARK_GREY, VGA_COLOR_BLACK, 70);
    } else {
        lp_puts(23, 1,
            "Up/Dn=Select  Enter=Open  \x11/Bksp=Up  F2=Source  F3=Rescan  Esc=Exit",
            VGA_COLOR_DARK_GREY, VGA_COLOR_BLACK, 70);
    }
}

/* Show a brief "Probing hardware..." overlay while re-scanning for media. */
static void lp_draw_probing(int src) {
    lp_fill(3, 0, ' ', VGA_COLOR_WHITE, VGA_COLOR_BLACK, 80);
    lp_fill(4, 0, ' ', VGA_COLOR_WHITE, VGA_COLOR_BLACK, 80);
    lp_puts(4, 4, "Probing hardware for ", VGA_COLOR_LIGHT_CYAN, VGA_COLOR_BLACK, 21);
    lp_puts(4, 25, lp_src_label[src], VGA_COLOR_WHITE, VGA_COLOR_BLACK, 12);
    lp_puts(4, 37, "  please wait...", VGA_COLOR_DARK_GREY, VGA_COLOR_BLACK, 18);
    for (int r = 5; r <= 20; r++)
        lp_fill(r, 0, ' ', VGA_COLOR_WHITE, VGA_COLOR_BLACK, 80);
}

static void lp_draw_footer(void) {
    lp_fill(24, 0, ' ', VGA_COLOR_BLACK, VGA_COLOR_CYAN, 80);
    lp_puts(24, 2,  "inteiliDOS LaunchPad 1.0",
            VGA_COLOR_BLACK, VGA_COLOR_CYAN, 28);
    lp_puts(24, 46, "ISO 9660  |  FAT12  |  IPGM  |  ELF",
            VGA_COLOR_BLACK, VGA_COLOR_CYAN, 34);
}

static void lp_draw_all(int src, int sel, int top,
                         const cdrom_drive_t *drives) {
    lp_draw_title();
    lp_draw_source_bar(src, drives);
    lp_draw_header();
    lp_draw_list(sel, top);
    lp_draw_sep();
    lp_draw_desc(sel);
    lp_draw_hints();
    lp_draw_footer();
}

/* Show a "Scanning..." overlay while reading the directory. */
static void lp_draw_scanning(int src) {
    lp_fill(3, 0, ' ', VGA_COLOR_WHITE, VGA_COLOR_BLACK, 80);
    lp_fill(4, 0, ' ', VGA_COLOR_WHITE, VGA_COLOR_BLACK, 80);
    lp_puts(4, 4, "Reading directory from ", VGA_COLOR_LIGHT_CYAN, VGA_COLOR_BLACK, 23);
    lp_puts(4, 27, lp_src_label[src], VGA_COLOR_WHITE, VGA_COLOR_BLACK, 12);
    lp_puts(4, 39, "  please wait...", VGA_COLOR_DARK_GREY, VGA_COLOR_BLACK, 18);
    for (int r = 5; r <= 20; r++)
        lp_fill(r, 0, ' ', VGA_COLOR_WHITE, VGA_COLOR_BLACK, 80);
}

/* =========================================================================
 * Loading dialog
 * ========================================================================= */
#define DLG_R0   5    /* dialog top row     */
#define DLG_R1  19    /* dialog bottom row  */
#define DLG_C0   4    /* dialog left col    */
#define DLG_C1  75    /* dialog right col   */
#define DLG_IW  (DLG_C1 - DLG_C0 - 1)   /* inner width = 70 */

static void lp_dlg_border(void) {
    lp_poke(DLG_R0, DLG_C0,  '+', VGA_COLOR_WHITE, VGA_COLOR_BLACK);
    lp_poke(DLG_R0, DLG_C1,  '+', VGA_COLOR_WHITE, VGA_COLOR_BLACK);
    lp_poke(DLG_R1, DLG_C0,  '+', VGA_COLOR_WHITE, VGA_COLOR_BLACK);
    lp_poke(DLG_R1, DLG_C1,  '+', VGA_COLOR_WHITE, VGA_COLOR_BLACK);
    for (int c = DLG_C0 + 1; c < DLG_C1; c++) {
        lp_poke(DLG_R0, c, '-', VGA_COLOR_WHITE, VGA_COLOR_BLACK);
        lp_poke(DLG_R1, c, '-', VGA_COLOR_WHITE, VGA_COLOR_BLACK);
    }
    for (int r = DLG_R0 + 1; r < DLG_R1; r++) {
        lp_poke(r, DLG_C0, '|', VGA_COLOR_WHITE, VGA_COLOR_BLACK);
        lp_fill(r, DLG_C0 + 1, ' ', VGA_COLOR_WHITE, VGA_COLOR_BLACK, DLG_IW);
        lp_poke(r, DLG_C1, '|', VGA_COLOR_WHITE, VGA_COLOR_BLACK);
    }
}

static void lp_dlg_line(int r, int indent, const char *s,
                          vga_color_t fg, int max_w) {
    lp_puts(r, DLG_C0 + 1 + indent, s, fg, VGA_COLOR_BLACK,
            max_w > 0 ? max_w : DLG_IW);
}

/*
 * Load buffer: physical address 0x00500000 (5 MB).
 * Safely above the kernel + BSS on any system with >= 8 MB RAM.
 * No paging is active, so direct physical pointer cast is valid.
 */
#define LP_MAX_LOAD  (4u * 1024u * 1024u)
static uint8_t *const lp_load_buf = (uint8_t *)IPGM_LOAD_ADDR;

static void lp_do_load(int src, int sel, const cdrom_drive_t *drives) {
    if (!lp_listing_ok || sel < 0 || sel >= lp_file_count) return;
    lp_file_t *f = &lp_files[sel];

    /* Directories are handled in the event loop before lp_do_load is called. */
    if (f->is_dir) return;
    if (f->size == 0) {
        lp_dlg_border();
        lp_dlg_line(DLG_R0 + 2, 1, "File is empty.", VGA_COLOR_LIGHT_RED, 40);
        lp_dlg_line(DLG_R0 + 4, 1, "Press any key.", VGA_COLOR_DARK_GREY, 30);
        keyboard_getchar();
        return;
    }
    if (f->size > LP_MAX_LOAD) {
        lp_dlg_border();
        lp_dlg_line(DLG_R0 + 2, 1, "File too large (> 4 MB).", VGA_COLOR_LIGHT_RED, 50);
        lp_dlg_line(DLG_R0 + 4, 1, "Press any key.", VGA_COLOR_DARK_GREY, 30);
        keyboard_getchar();
        return;
    }

    /* ── Show loading dialog ── */
    lp_dlg_border();
    lp_dlg_line(DLG_R0 + 1, 1, "LaunchPad -- Loading",
                VGA_COLOR_LIGHT_CYAN, 50);

    lp_dlg_line(DLG_R0 + 3, 1, "File   : ", VGA_COLOR_DARK_GREY, 9);
    lp_dlg_line(DLG_R0 + 3, 10, f->name, VGA_COLOR_WHITE, 30);

    lp_dlg_line(DLG_R0 + 4, 1, "Source : ", VGA_COLOR_DARK_GREY, 9);
    lp_dlg_line(DLG_R0 + 4, 10, lp_src_label[src], VGA_COLOR_WHITE, 15);

    lp_dlg_line(DLG_R0 + 5, 1, "Size   : ", VGA_COLOR_DARK_GREY, 9);
    lp_put_uint_r(DLG_R0 + 5, DLG_C0 + 11, f->size / 1024u + 1u, 6,
                  VGA_COLOR_WHITE, VGA_COLOR_BLACK);
    lp_dlg_line(DLG_R0 + 5, 17, " KB", VGA_COLOR_WHITE, 3);

    lp_dlg_line(DLG_R0 + 7, 1,
                "Reading from hardware -- drive LED should be active...",
                VGA_COLOR_DARK_GREY, 65);

    /* ── Perform the actual read ── */
    int32_t bytes = 0;

    if (lp_src_is_cdrom(src)) {
        uint8_t di = (uint8_t)src;
        /* Construct a temporary dirent from cached LBA. */
        iso9660_dirent_t ent;
        for (int i = 0; i < ISO9660_NAME_MAX; i++) ent.name[i] = f->name[i];
        ent.lba    = f->cd_lba;
        ent.size   = f->size;
        ent.is_dir = f->is_dir;
        bytes = iso9660_read_file(di, &ent, lp_load_buf, LP_MAX_LOAD);
    } else {
        /* Floppy */
        uint8_t flop = (uint8_t)(src - LP_SRC_FLOPPY_A);
        fat12_dirent_t ent;
        for (int i = 0; i < FAT12_NAME_MAX; i++) ent.name[i] = f->name[i];
        ent.size          = f->size;
        ent.first_cluster = f->fat_cluster;
        ent.attr          = 0x20u;  /* archive */
        bytes = fat12_read_file(flop, &ent, lp_load_buf, LP_MAX_LOAD);
    }

    if (bytes <= 0) {
        lp_dlg_line(DLG_R0 + 9, 1, "ERROR: Read failed.",
                    VGA_COLOR_LIGHT_RED, 50);

        /*
         * For floppy reads, fdc_read_sector() returns a distinct code for
         * each failure mode (timeout, abnormal termination, CRC).  Retrieve
         * it so we can show the user a meaningful diagnosis rather than a
         * generic "read failed" message.
         *
         * CD-ROM failures are not further classified here; the ISO 9660
         * layer already prints its own error into lp_err when appropriate.
         */
        if (!lp_src_is_cdrom(src)) {
            int ferr = fdc_last_error_get();
            const char *detail;
            const char *hint;
            if (ferr == FDC_ERR_TIMEOUT) {
                detail = "Drive timeout -- no disk or controller not ready.";
                hint   = "Insert a formatted floppy and press F2 to retry.";
            } else if (ferr == FDC_ERR_ABNORMAL) {
                detail = "Abnormal termination -- disk missing or unformatted.";
                hint   = "Check disk is inserted and FAT12-formatted, then F2.";
            } else if (ferr == FDC_ERR_CRC) {
                detail = "CRC error -- disk surface may be damaged.";
                hint   = "Try a different floppy disk.";
            } else {
                detail = "Seek or parameter error.";
                hint   = "Check the floppy drive and disk, then try again.";
            }
            lp_dlg_line(DLG_R0 + 11, 1, detail, VGA_COLOR_DARK_GREY, 68);
            lp_dlg_line(DLG_R0 + 12, 1, hint,   VGA_COLOR_DARK_GREY, 68);
        } else {
            lp_dlg_line(DLG_R0 + 11, 1, "Check the disc and try again.",
                        VGA_COLOR_DARK_GREY, 50);
        }

        lp_dlg_line(DLG_R0 + 14, 1, "Press any key.", VGA_COLOR_DARK_GREY, 30);
        keyboard_getchar();
        return;
    }

    /* ── Try to execute ── */
    lp_dlg_line(DLG_R0 + 9,  1, "Loaded successfully.", VGA_COLOR_LIGHT_GREEN, 40);
    lp_put_uint_r(DLG_R0 + 10, DLG_C0 + 2, (uint32_t)bytes, 7,
                  VGA_COLOR_WHITE, VGA_COLOR_BLACK);
    lp_dlg_line(DLG_R0 + 10, 9, " bytes read into RAM at 0x00500000",
                VGA_COLOR_WHITE, 40);

    /* Identify the executable format by magic bytes. */
    int is_ipgm = (bytes >= 16
                   && lp_load_buf[0] == 'I'
                   && lp_load_buf[1] == 'P'
                   && lp_load_buf[2] == 'G'
                   && lp_load_buf[3] == 'M');

    int is_elf  = (bytes >= 16
                   && lp_load_buf[0] == 0x7F
                   && lp_load_buf[1] == 'E'
                   && lp_load_buf[2] == 'L'
                   && lp_load_buf[3] == 'F');

    if (!is_ipgm && !is_elf) {
        lp_dlg_line(DLG_R0 + 12, 1,
                    "Not an IPGM or ELF executable -- file is in RAM but",
                    VGA_COLOR_DARK_GREY, 65);
        lp_dlg_line(DLG_R0 + 13, 1,
                    "cannot be launched without a recognised header.",
                    VGA_COLOR_DARK_GREY, 65);
        lp_dlg_line(DLG_R0 + 15, 1, "Press any key to return.",
                    VGA_COLOR_DARK_GREY, 40);
        keyboard_getchar();
        return;
    }

    if (is_ipgm) {
        lp_dlg_line(DLG_R0 + 12, 1, "IPGM header valid.  Launching...",
                    VGA_COLOR_LIGHT_GREEN, 50);
    } else {
        lp_dlg_line(DLG_R0 + 12, 1, "ELF32 header valid.  Launching...",
                    VGA_COLOR_LIGHT_GREEN, 50);
    }
    lp_dlg_line(DLG_R0 + 13, 1,
                "The shell will resume when the program returns.",
                VGA_COLOR_DARK_GREY, 60);
    lp_dlg_line(DLG_R0 + 14, 1, "Press any key to launch.",
                VGA_COLOR_DARK_GREY, 40);
    keyboard_getchar();

    /* ── Run the program ── */
    vga_clear();
    int rc;
    if (is_ipgm) {
        rc = loader_exec(lp_load_buf, (uint32_t)bytes);
    } else {
        rc = loader_exec_elf(lp_load_buf, (uint32_t)bytes);
    }

    /* Program has returned — redraw will happen in the main loop. */
    if (rc != 0) {
        if (is_elf) {
            vga_puts("LaunchPad: ELF loader error (bad header or segment).\n");
        } else {
            vga_puts("LaunchPad: loader_exec rejected the IPGM header.\n");
        }
        vga_puts("Press any key.\n");
        keyboard_getchar();
    }
}

/* =========================================================================
 * Main entry point
 * ========================================================================= */
void launchpad_run(void) {
    /*
     * Take a mutable local copy of the drive table so we can update it at
     * runtime when the user inserts a disc or swaps a floppy after boot.
     * cdrom_rescan_media() updates the internal table in cdrom.c; we then
     * sync our local copy from cdrom_drives().
     */
    cdrom_drive_t drives[CDROM_MAX_DRIVES];
    {
        const cdrom_drive_t *tbl = cdrom_drives();
        for (int i = 0; i < CDROM_MAX_DRIVES; i++)
            drives[i] = tbl[i];
    }

    /* Choose starting source: first CD-ROM that has a disc, else Floppy A:. */
    int src = LP_SRC_FLOPPY_A;
    for (int i = 0; i < 4; i++) {
        if ((uint8_t)i < CDROM_MAX_DRIVES && drives[i].present == CDROM_PRESENT) {
            src = i;
            if (drives[i].last_lba > 0) break;
        }
    }

    int sel = 0;
    int top = 0;
    lp_depth = 0;   /* always start at root, even on re-entry */

    /* Initial directory scan. */
    lp_draw_title();
    lp_draw_source_bar(src, drives);
    lp_draw_header();
    lp_draw_sep();
    lp_draw_hints();
    lp_draw_footer();
    lp_draw_scanning(src);
    lp_refresh_listing(src, drives);
    lp_draw_all(src, sel, top, drives);

    for (;;) {
        int ch = keyboard_getchar();

        if (ch == KEY_ESCAPE || ch == 'q' || ch == 'Q') {
            break;
        }
        else if (ch == KEY_UP) {
            if (sel > 0) {
                sel--;
                if (sel < top) top = sel;
                lp_draw_list(sel, top);
                lp_draw_desc(sel);
            }
        }
        else if (ch == KEY_DOWN) {
            if (lp_listing_ok && sel < lp_file_count - 1) {
                sel++;
                if (sel >= top + 18) top = sel - 17;
                lp_draw_list(sel, top);
                lp_draw_desc(sel);
            }
        }
        else if (ch == LP_F2) {
            /* Cycle source — reset to root of new source, then probe. */
            src = (src + 1) % LP_SRC_COUNT;
            lp_depth = 0;
            sel = 0; top = 0;
            lp_draw_source_bar(src, drives);
            lp_draw_probing(src);
            lp_probe_source(src, drives);
            lp_draw_scanning(src);
            lp_refresh_listing(src, drives);
            lp_draw_all(src, sel, top, drives);
        }
        else if (ch == LP_F3) {
            /*
             * Manual rescan — re-probe hardware, stay in current directory.
             * Useful when a disc is inserted or a floppy is swapped.
             * Reset to root because the directory tree may have changed.
             */
            lp_depth = 0;
            sel = 0; top = 0;
            lp_draw_source_bar(src, drives);
            lp_draw_probing(src);
            lp_probe_source(src, drives);
            lp_draw_scanning(src);
            lp_refresh_listing(src, drives);
            lp_draw_all(src, sel, top, drives);
        }
        else if (ch == LP_F5) {
            if (lp_src_is_cdrom(src)) {
                /* Eject tray — go back to root, re-probe, refresh. */
                lp_depth = 0;
                cdrom_eject((uint8_t)src);
                lp_probe_source(src, drives);
                sel = 0; top = 0;
                lp_refresh_listing(src, drives);
                lp_draw_all(src, sel, top, drives);
            }
        }
        else if (ch == KEY_BACKSPACE || ch == KEY_LEFT) {
            /* Go up one directory level. */
            if (lp_depth > 0) {
                lp_depth--;
                sel = 0; top = 0;
                lp_draw_scanning(src);
                lp_refresh_listing(src, drives);
                lp_draw_all(src, sel, top, drives);
            }
        }
        else if (ch == KEY_ENTER) {
            if (lp_listing_ok && sel >= 0 && sel < lp_file_count
                && lp_files[sel].is_dir) {
                /* Navigate into the selected directory. */
                if (lp_depth < LP_DIR_DEPTH - 1) {
                    lp_dir_t *d = &lp_dirstack[lp_depth];
                    lp_strcpy(d->name, lp_files[sel].name, ISO9660_NAME_MAX);
                    d->cd_lba     = lp_files[sel].cd_lba;
                    d->cd_size    = lp_files[sel].size;   /* dir byte length */
                    d->fat_cluster = lp_files[sel].fat_cluster;
                    lp_depth++;
                }
                sel = 0; top = 0;
                lp_draw_scanning(src);
                lp_refresh_listing(src, drives);
                lp_draw_all(src, sel, top, drives);
            } else {
                lp_do_load(src, sel, drives);
                /* After load/run, restore the full LaunchPad screen. */
                lp_draw_all(src, sel, top, drives);
            }
        }
    }

    vga_clear();
}
