/*
 * inteilidOS -- shell/commands.c
 * Built-in commands + IntelliShell NLP translator
 */

#include "commands.h"
#include "shell.h"
#include "iedit.h"
#include "tour.h"
#include "../tetris/tetris.h"
#include "basic.h"
#include "sheets.h"
#include "talk.h"
#include "filemanager.h"
#include "launchpad.h"
#include "setup.h"
#include "../still_alive_easter_egg/stillalive.h"
#include "../daisy_bell_easter_egg/daisy.h"
#include "../kernel/vga.h"
#include "../kernel/keyboard.h"
#include "../kernel/memory.h"
#include "../kernel/timer.h"
#include "../kernel/cdrom.h"
#include "../kernel/ata.h"
#include <stdint.h>
#include <stddef.h>

/* =========================================================================
 * Helpers
 * ========================================================================= */
static void println(const char *s) { vga_puts(s); vga_putchar('\n'); }

static void print_banner_line(const char *label, const char *val) {
    vga_set_color(VGA_COLOR_LIGHT_CYAN, VGA_COLOR_BLACK);
    vga_puts(label);
    vga_set_color(VGA_COLOR_WHITE, VGA_COLOR_BLACK);
    vga_puts(val);
    vga_putchar('\n');
}

/* =========================================================================
 * Individual command implementations
 * ========================================================================= */

/* DIR -- list directory contents (simulated) */
static int cmd_dir(int argc, const char *argv[]) {
    (void)argc; (void)argv;
    vga_set_color(VGA_COLOR_LIGHT_CYAN, VGA_COLOR_BLACK);
    vga_printf(" Directory of %s\n\n", cwd);
    vga_set_color(VGA_COLOR_WHITE, VGA_COLOR_BLACK);

    /* Static demo listing for root */
    struct { const char *name; const char *type; } entries[] = {
        {"BIN",      "<DIR>"},
        {"CONFIG",   "<DIR>"},
        {"APPS",     "<DIR>"},
        {"USERS",    "<DIR>"},
        {"SYSTEM",   "<DIR>"},
        {"PACKAGES", "<DIR>"},
        {"TEMP",     "<DIR>"},
        {"LOGS",     "<DIR>"},
        {"README.TXT", "     243"},
        {"AUTOEXEC.BAT","     128"},
        {NULL, NULL}
    };
    int files = 0, dirs = 0;
    for (int i = 0; entries[i].name; i++) {
        if (entries[i].type[0] == '<') {
            vga_set_color(VGA_COLOR_LIGHT_BLUE, VGA_COLOR_BLACK);
            dirs++;
        } else {
            vga_set_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK);
            files++;
        }
        vga_printf("  %-20s  %s\n", entries[i].name, entries[i].type);
    }
    vga_set_color(VGA_COLOR_WHITE, VGA_COLOR_BLACK);
    vga_printf("\n  %d file(s)  %d dir(s)\n", files, dirs);
    return 0;
}

/* CD -- change directory */
static int cmd_cd(int argc, const char *argv[]) {
    if (argc < 1) {
        vga_printf("Current directory: %s\n", cwd);
        return 0;
    }
    const char *dest = argv[0];
    if (kstrcmp(dest, "..") == 0) {
        /* Go up one level */
        int len = (int)kstrlen(cwd);
        while (len > 3 && cwd[len-1] != '\\') len--;
        if (len > 3) len--;
        cwd[len] = '\0';
        if (kstrlen(cwd) < 3) { cwd[2] = '\\'; cwd[3] = '\0'; }
    } else if (kstrcmp(dest, "\\") == 0) {
        cwd[2] = '\\'; cwd[3] = '\0';
    } else {
        /* Append */
        char tmp[256];
        kstrcpy(tmp, cwd);
        if (tmp[kstrlen(tmp)-1] != '\\') kstrcat(tmp, "\\");
        /* Upper-case dest */
        char upper[64];
        kstrncpy(upper, dest, 63);
        for (char *p = upper; *p; p++) if (*p >= 'a' && *p <= 'z') *p -= 32;
        kstrcat(tmp, upper);
        if (kstrlen(tmp) < 256) kstrcpy(cwd, tmp);
    }
    return 0;
}

/* CLS -- clear screen */
static int cmd_cls(int argc, const char *argv[]) {
    (void)argc; (void)argv;
    vga_clear();
    return 0;
}

/* HELP -- list commands */
static int cmd_help(int argc, const char *argv[]) {
    (void)argc; (void)argv;

    vga_set_color(VGA_COLOR_BLACK, VGA_COLOR_LIGHT_CYAN);
    vga_puts("  inteiliDOS Help                                                               ");
    vga_set_color(VGA_COLOR_LIGHT_GREEN, VGA_COLOR_BLACK);
    println("\n");

    /* File & directory */
    vga_set_color(VGA_COLOR_BROWN, VGA_COLOR_BLACK);
    println("  -- Files & Directories --------");
    vga_set_color(VGA_COLOR_LIGHT_CYAN, VGA_COLOR_BLACK);
    println("  DIR      - List directory contents");
    println("  CD       - Change directory");
    println("  MKDIR    - Create a directory");
    println("  TREE     - Display directory tree");
    println("  COPY     - Copy a file");
    println("  MOVE     - Move a file");
    println("  DELETE   - Delete a file");
    println("  TYPE     - Display file contents");

    /* Storage */
    vga_set_color(VGA_COLOR_BROWN, VGA_COLOR_BLACK);
    println("\n  -- Storage --------------------");
    vga_set_color(VGA_COLOR_LIGHT_CYAN, VGA_COLOR_BLACK);
    println("  DISKCHECK - Check IDE hard disk health and geometry");

    /* System */
    vga_set_color(VGA_COLOR_BROWN, VGA_COLOR_BLACK);
    println("\n  -- System ---------------------");
    vga_set_color(VGA_COLOR_LIGHT_CYAN, VGA_COLOR_BLACK);
    println("  CLS      - Clear the screen");
    println("  MEM      - Show memory usage");
    println("  SYSINFO  - Detailed system information");
    println("  TIME     - Display system time");
    println("  DATE     - Display system date");
    println("  HISTORY  - Show command history");
    println("  SHUTDOWN - Power off");
    println("  RESTART  - Reboot");

    /* Apps */
    vga_set_color(VGA_COLOR_BROWN, VGA_COLOR_BLACK);
    println("\n  -- Applications ---------------");
    vga_set_color(VGA_COLOR_LIGHT_CYAN, VGA_COLOR_BLACK);
    println("  IEDIT    - IEdit full-screen text editor");
    println("  BASIC    - InteiliBASIC interpreter");
    println("  SHEETS   - InteiliSheets spreadsheet (=SUM / =AVG formulas)");
    println("  TALK <text>    - Speak text via PC speaker (SAM TTS)");
    println("  VOLUME [0-100%%] - Get or set PC speaker volume (default 50%%)");
    println("  DEMO     - inteiliDOS feature showcase");

    /* Games */
    vga_set_color(VGA_COLOR_BROWN, VGA_COLOR_BLACK);
    println("\n  -- Games ----------------------");
    vga_set_color(VGA_COLOR_LIGHT_CYAN, VGA_COLOR_BLACK);
    println("  TETRIS   - Play Tetris (Tetris A-Theme plays in the background)");
    println("             Keyboard + USB gamepad supported (D-pad / face btns)");

    /* Extras */
    vga_set_color(VGA_COLOR_BROWN, VGA_COLOR_BLACK);
    println("\n  -- Extras ---------------------");
    vga_set_color(VGA_COLOR_LIGHT_CYAN, VGA_COLOR_BLACK);
    println("  ABOUT    - About inteiliDOS");
    println("  HELLO    - Say hello");
    println("  FM       - InteiliFile Manager (graphical file browser)");
    println("  PROGRAM  - LaunchPad: load programs from CD-ROM or floppy into RAM");
    println("  SETUP    - Install inteiliDOS to HDD/CompactFlash (setup wizard)");

    vga_set_color(VGA_COLOR_WHITE, VGA_COLOR_BLACK);
    println("\n  IntelliShell also understands plain English:");
    vga_set_color(VGA_COLOR_DARK_GREY, VGA_COLOR_BLACK);
    println("  e.g.  \"show files\"  \"make a folder named WORK\"  \"where am i\"");
    vga_set_color(VGA_COLOR_WHITE, VGA_COLOR_BLACK);
    vga_putchar('\n');
    return 0;
}

/* MEM -- memory info */
static int cmd_mem(int argc, const char *argv[]) {
    (void)argc; (void)argv;
    size_t total = memory_total_kb();
    size_t free_  = memory_free_kb();
    size_t used  = total > free_ ? total - free_ : 0;
    vga_set_color(VGA_COLOR_LIGHT_CYAN, VGA_COLOR_BLACK);
    println("\nMemory Status");
    println("=============");
    vga_set_color(VGA_COLOR_WHITE, VGA_COLOR_BLACK);
    vga_printf("  Total  : %u KB (%u MB)\n", (unsigned)total, (unsigned)(total/1024));
    vga_printf("  Used   : %u KB\n", (unsigned)used);
    vga_printf("  Free   : %u KB\n", (unsigned)free_);
    return 0;
}

/* SYSINFO / ABOUT */
static int cmd_sysinfo(int argc, const char *argv[]) {
    (void)argc; (void)argv;
    vga_set_color(VGA_COLOR_LIGHT_GREEN, VGA_COLOR_BLACK);
    println("\n  inteiliDOS System Information");
    println("  ==============================");
    vga_set_color(VGA_COLOR_WHITE, VGA_COLOR_BLACK);
    vga_printf("  OS      : inteiliDOS Version 1.0\n");
    vga_printf("  Vendor  : Inteilix Software Corporation\n");
    vga_printf("  Arch    : x86 (IA-32 Protected Mode)\n");
    vga_printf("  Shell   : IntelliShell 1.0\n");
    vga_printf("  FS      : IFS (Inteilix File System)\n");
    size_t total = memory_total_kb();
    vga_printf("  Memory  : %u KB total\n", (unsigned)total);
    vga_printf("  Ticks   : %u\n", timer_get_ticks());
    return 0;
}

static int cmd_about(int argc, const char *argv[]) {
    (void)argc; (void)argv;
    vga_set_color(VGA_COLOR_LIGHT_CYAN, VGA_COLOR_BLACK);
    println("\n  inteiliDOS");
    vga_set_color(VGA_COLOR_WHITE, VGA_COLOR_BLACK);
    println("  Developed by Inteilix Software Corporation");
    println("  \"The future still has a blinking cursor.\"");
    println("  Version 1.0");
    println("  The command line never stopped evolving.");
    return 0;
}

/* HELLO easter egg */
static int cmd_hello(int argc, const char *argv[]) {
    (void)argc; (void)argv;
    vga_set_color(VGA_COLOR_LIGHT_GREEN, VGA_COLOR_BLACK);
    println("Hello!");
    vga_set_color(VGA_COLOR_WHITE, VGA_COLOR_BLACK);
    println("Need help? Type HELP.");
    return 0;
}

/* SETUP -- OS installation wizard */
static int cmd_setup(int argc, const char *argv[]) {
    (void)argc; (void)argv;
    setup_run();
    return 0;
}

/* FM / FILEMANAGER -- graphical file manager */
static int cmd_filemanager(int argc, const char *argv[]) {
    (void)argc; (void)argv;
    filemanager_run();
    return 0;
}

/* TYPE -- display file */
static int cmd_type(int argc, const char *argv[]) {
    if (argc < 1) { println("Usage: TYPE <filename>"); return 1; }
    vga_printf("[Simulated] Contents of %s:\n", argv[0]);
    vga_set_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK);
    println("  (File I/O requires IFS driver)");
    vga_set_color(VGA_COLOR_WHITE, VGA_COLOR_BLACK);
    return 0;
}

/* MKDIR */
static int cmd_mkdir(int argc, const char *argv[]) {
    if (argc < 1) { println("Usage: MKDIR <dirname>"); return 1; }
    vga_printf("Creating directory: %s\n", argv[0]);
    println("Done.");
    return 0;
}

/* COPY / MOVE / DELETE */
static int cmd_copy(int argc, const char *argv[]) {
    if (argc < 2) { println("Usage: COPY <src> <dst>"); return 1; }
    vga_printf("Copying %s -> %s\n", argv[0], argv[1]);
    println("Done.");
    return 0;
}
static int cmd_move(int argc, const char *argv[]) {
    if (argc < 2) { println("Usage: MOVE <src> <dst>"); return 1; }
    vga_printf("Moving %s -> %s\n", argv[0], argv[1]);
    println("Done.");
    return 0;
}
static int cmd_delete(int argc, const char *argv[]) {
    if (argc < 1) { println("Usage: DELETE <filename>"); return 1; }
    vga_printf("Deleting: %s\n", argv[0]);
    println("Done.");
    return 0;
}

/* TREE */
static int cmd_tree(int argc, const char *argv[]) {
    (void)argc; (void)argv;
    vga_set_color(VGA_COLOR_LIGHT_CYAN, VGA_COLOR_BLACK);
    vga_printf("%s\n", cwd);
    vga_set_color(VGA_COLOR_WHITE, VGA_COLOR_BLACK);
    println("|-- BIN");
    println("|-- CONFIG");
    println("|-- APPS");
    println("|   |-- GAMES");
    println("|   `-- TOOLS");
    println("|-- USERS");
    println("|-- SYSTEM");
    println("|-- PACKAGES");
    println("|-- TEMP");
    println("`-- LOGS");
    return 0;
}

/* HISTORY */
static int cmd_history(int argc, const char *argv[]) {
    (void)argc; (void)argv;
    if (history_count == 0) { println("No history."); return 0; }
    for (int i = 0; i < history_count; i++)
        vga_printf("  %d  %s\n", i + 1, history[i]);
    return 0;
}

/* TIME / DATE */
static int cmd_time(int argc, const char *argv[]) {
    (void)argc; (void)argv;
    uint32_t t = timer_get_ticks();
    vga_printf("System time: %u ticks (%.1f s)\n",
               t, (unsigned)(t / 1000));
    return 0;
}
static int cmd_date(int argc, const char *argv[]) {
    (void)argc; (void)argv;
    println("Date: [RTC driver required for real date]");
    return 0;
}

/* INSTALL / REMOVE / UPDATE / SEARCH */
static int cmd_install(int argc, const char *argv[]) {
    if (argc < 1) { println("Usage: INSTALL <package>"); return 1; }
    vga_set_color(VGA_COLOR_LIGHT_GREEN, VGA_COLOR_BLACK);
    vga_printf("Searching Inteili Package Repository for '%s'...\n", argv[0]);
    vga_set_color(VGA_COLOR_WHITE, VGA_COLOR_BLACK);
    println("Downloading...");
    timer_sleep(300);
    println("Installing...");
    timer_sleep(200);
    println("Done.");
    vga_printf("Package '%s' installed successfully.\n", argv[0]);
    return 0;
}
static int cmd_remove(int argc, const char *argv[]) {
    if (argc < 1) { println("Usage: REMOVE <package>"); return 1; }
    vga_printf("Removing '%s'...\n", argv[0]);
    timer_sleep(200);
    println("Done.");
    return 0;
}
static int cmd_update(int argc, const char *argv[]) {
    (void)argv;
    if (argc >= 1) println("Checking for updates...");
    else           println("Usage: UPDATE <package|ALL>");
    timer_sleep(300);
    println("All packages are up to date.");
    return 0;
}
static int cmd_search(int argc, const char *argv[]) {
    if (argc < 1) { println("Usage: SEARCH <query>"); return 1; }
    vga_set_color(VGA_COLOR_LIGHT_CYAN, VGA_COLOR_BLACK);
    vga_printf("Results for '%s':\n", argv[0]);
    vga_set_color(VGA_COLOR_WHITE, VGA_COLOR_BLACK);
    /* Simulate repository results */
    if (kstrstr(argv[0], "editor") || kstrstr(argv[0], "EDITOR")) {
        println("  TextEdit  - Lightweight text editor");
        println("  Nano      - Unix-style editor");
        println("  Vim       - Modal editor");
        println("  Micro     - Modern terminal editor");
        println("  Markdown Studio - Markdown-focused editor");
    } else if (kstrstr(argv[0], "game") || kstrstr(argv[0], "GAME")) {
        println("  Snake     - Classic snake game");
        println("  Mines     - Minesweeper");
        println("  Chess     - Chess engine");
        println("  Sudoku    - Sudoku puzzle");
    } else {
        vga_printf("  %s-utils   - %s utilities package\n", argv[0], argv[0]);
        vga_printf("  lib%s      - %s library\n", argv[0], argv[0]);
    }
    return 0;
}

/* BACKUP / RESTORE */
static int cmd_backup(int argc, const char *argv[]) {
    const char *target = (argc >= 1) ? argv[0] : cwd;
    vga_printf("Creating archive of '%s'...\n", target);
    timer_sleep(400);
    println("Done.");
    return 0;
}
static int cmd_restore(int argc, const char *argv[]) {
    const char *target = (argc >= 1) ? argv[0] : "latest";
    vga_printf("Restoring from backup '%s'...\n", target);
    timer_sleep(400);
    println("Done.");
    return 0;
}

/* TETRIS -- classic Tetris game with background music */
static int cmd_tetris(int argc, const char *argv[]) {
    (void)argc; (void)argv;
    tetris_run();
    return 0;
}

/* TOUR -- text-adventure game (kept for DEMO showcase) */
static int cmd_tour(int argc, const char *argv[]) {
    (void)argc; (void)argv;
    tour_run();
    return 0;
}

/* IEDIT -- full-screen text editor */
static int cmd_iedit(int argc, const char *argv[]) {
    const char *filename = (argc >= 1) ? argv[0] : NULL;
    iedit_run(filename);
    return 0;
}

/* BASIC -- InteiliBASIC stub */
static int cmd_basic(int argc, const char *argv[]) {
    (void)argc; (void)argv;
    basic_run();
    return 0;
}

/* QUIT -- already in IntelliShell */
static int cmd_quit(int argc, const char *argv[]) {
    (void)argc; (void)argv;
    vga_set_color(VGA_COLOR_LIGHT_CYAN, VGA_COLOR_BLACK);
    println("You are already in IntelliShell.");
    vga_set_color(VGA_COLOR_WHITE, VGA_COLOR_BLACK);
    println("Type HELP for a list of commands, or SHUTDOWN to power off.");
    return 0;
}

/* DAISY -- Easter egg: draws IBM 7094 console art and plays Daisy Bell */
static int cmd_daisy(int argc, const char *argv[]) {
    (void)argc; (void)argv;
    daisy_play();
    return 0;
}

/* STILL ALIVE -- Easter egg: plays "Still Alive" through the PC speaker */
static int cmd_stillalive(int argc, const char *argv[]) {
    (void)argc; (void)argv;
    stillalive_play();
    return 0;
}

/* SHEETS -- InteiliSheets spreadsheet */
static int cmd_sheets(int argc, const char *argv[]) {
    (void)argc; (void)argv;
    sheets_run();
    return 0;
}

/* VOLUME -- get/set PC speaker volume */
static int cmd_volume(int argc, const char *argv[]) {
    if (argc == 0) {
        /* No argument: report current level */
        int v = speaker_get_volume();
        vga_printf("  Volume: %d%%\n", v);
        return 0;
    }

    /* Parse argument: accept "85", "85%", or "85 %" */
    const char *arg = argv[0];
    int val = 0;
    int i   = 0;

    /* Skip leading spaces */
    while (arg[i] == ' ') i++;

    /* Read digits */
    if (arg[i] < '0' || arg[i] > '9') {
        println("  Usage: VOLUME [0-100]  or  VOLUME 85%");
        return 1;
    }
    while (arg[i] >= '0' && arg[i] <= '9') {
        val = val * 10 + (arg[i] - '0');
        i++;
    }

    /* Optional trailing '%' */
    while (arg[i] == ' ') i++;
    if (arg[i] != '%' && arg[i] != '\0') {
        println("  Usage: VOLUME [0-100]  or  VOLUME 85%");
        return 1;
    }

    if (val < 0 || val > 100) {
        println("  Error: volume must be between 0 and 100.");
        return 1;
    }

    speaker_set_volume(val);
    vga_printf("  Volume set to %d%%.\n", val);

    /* Short confirmation beep (skipped automatically if val == 0) */
    speaker_beep(880, 60);
    speaker_off();

    return 0;
}

/* TALK -- InteiliTalk text-to-speech */
static int cmd_talk(int argc, const char *argv[]) {
    if (argc >= 1) {
        /* Join all tokens into one sentence and speak it */
        char sentence[256];
        int pos = 0;
        for (int i = 0; i < argc && pos < 254; i++) {
            if (i > 0 && pos < 254) { sentence[pos++] = ' '; }
            for (int j = 0; argv[i][j] && pos < 254; j++)
                sentence[pos++] = argv[i][j];
        }
        sentence[pos] = '\0';
        talk_speak(sentence);
        vga_putchar('\n');
        return 0;
    }
    vga_set_color(VGA_COLOR_LIGHT_CYAN, VGA_COLOR_BLACK);
    println("Usage: TALK <text to speak>");
    vga_set_color(VGA_COLOR_DARK_GREY, VGA_COLOR_BLACK);
    println("  Example:  TALK Hello world");
    println("  Example:  TALK inteiliDOS is ready");
    vga_set_color(VGA_COLOR_WHITE, VGA_COLOR_BLACK);
    return 0;
}

/* =========================================================================
 * DEMO -- inteiliDOS feature showcase
 * ========================================================================= */

/* ── Tetris preview helpers (used by Feature 8 of DEMO) ─────────────────
 * Draws a 10-wide × 8-row mini board and animates a falling S-piece.
 * keyboard_poll() is checked between frames so any key skips the preview.
 * ───────────────────────────────────────────────────────────────────────── */
static void demo_tetris_frame(int piece_row) {
    /* Static stack: 1 = filled cell, 0 = empty */
    static const uint8_t brd[8][10] = {
        {0,0,0,0,0,0,0,0,0,0},
        {0,0,0,0,0,0,0,0,0,0},
        {0,0,0,0,0,0,0,0,0,0},
        {0,0,0,0,0,0,0,0,0,0},
        {0,0,0,0,0,0,0,0,0,0},
        {0,0,0,1,0,0,0,0,0,0},
        {0,1,1,1,0,0,0,1,0,0},
        {1,1,1,1,0,0,1,1,1,1},
    };
    /* S-piece absolute column positions for each of its 4 cells:
     *   top half    → cols 5, 6
     *   bottom half → cols 4, 5                                          */
    static const int pr[4] = {0, 0, 1, 1};
    static const int pc[4] = {5, 6, 4, 5};

    /* Top border */
    vga_set_color(VGA_COLOR_DARK_GREY, VGA_COLOR_BLACK);
    vga_puts("  +----------+\n");

    for (int r = 0; r < 8; r++) {
        vga_set_color(VGA_COLOR_DARK_GREY, VGA_COLOR_BLACK);
        vga_puts("  |");
        for (int c = 0; c < 10; c++) {
            int is_piece = 0;
            for (int p = 0; p < 4; p++) {
                if (pr[p] + piece_row == r && pc[p] == c) {
                    is_piece = 1;
                    break;
                }
            }
            if (is_piece) {
                vga_set_color(VGA_COLOR_LIGHT_GREEN, VGA_COLOR_BLACK);
                vga_putchar('#');
            } else if (brd[r][c]) {
                vga_set_color(VGA_COLOR_LIGHT_CYAN, VGA_COLOR_BLACK);
                vga_putchar('#');
            } else {
                vga_set_color(VGA_COLOR_BLACK, VGA_COLOR_BLACK);
                vga_putchar(' ');
            }
        }
        vga_set_color(VGA_COLOR_DARK_GREY, VGA_COLOR_BLACK);
        vga_puts("|\n");
    }

    /* Bottom border */
    vga_set_color(VGA_COLOR_DARK_GREY, VGA_COLOR_BLACK);
    vga_puts("  +----------+\n");
}

static void demo_tetris_preview(void) {
    int start_row, start_col;
    vga_get_cursor(&start_row, &start_col);

    /* Draw the first frame to claim the screen rows */
    demo_tetris_frame(0);

    /* Animate: S-piece falls from row 0 down to row 6 (7 frames total) */
    for (int drop = 1; drop <= 6; drop++) {
        timer_sleep(220);
        if (keyboard_poll() >= 0) return; /* any key skips the preview */
        vga_set_cursor(start_row, 0);
        demo_tetris_frame(drop);
    }

    /* Brief pause on the final landed frame */
    timer_sleep(350);
}

/* Helper: type out a string one character at a time with a small delay,
 * simulating the look of a live typing session.
 */
static void demo_type(const char *s, uint32_t ms_per_char) {
    for (int i = 0; s[i]; i++) {
        vga_putchar(s[i]);
        timer_sleep(ms_per_char);
    }
}

/* Helper: wait up to 'ms' milliseconds for a keypress.
 * Returns immediately if the user presses any key.
 */
static void demo_pause(uint32_t ms) {
    uint32_t step = 40;
    while (ms > 0) {
        if (keyboard_poll() >= 0) return;
        timer_sleep(step);
        ms = (ms > step) ? ms - step : 0;
    }
}

/* Draw a coloured section banner */
static void demo_banner(const char *title, vga_color_t bg) {
    vga_set_color(VGA_COLOR_BLACK, bg);
    vga_puts("\n  ");
    vga_puts(title);
    /* Pad to 76 chars */
    int len = 2 + (int)kstrlen(title);
    for (int i = len; i < 78; i++) vga_putchar(' ');
    vga_set_color(VGA_COLOR_WHITE, VGA_COLOR_BLACK);
    vga_putchar('\n');
}

/* One-line pause prompt */
static void demo_any_key(void) {
    vga_set_color(VGA_COLOR_DARK_GREY, VGA_COLOR_BLACK);
    vga_puts("\n  [ Press any key for the next feature... ]\n");
    vga_set_color(VGA_COLOR_WHITE, VGA_COLOR_BLACK);
    keyboard_getchar();
}

static int cmd_demo(int argc, const char *argv[]) {
    (void)argc; (void)argv;

    /* ── Title screen ─────────────────────────────────────────────────── */
    vga_clear();
    vga_set_color(VGA_COLOR_LIGHT_CYAN, VGA_COLOR_BLACK);
    println(
        "\n"
        "  ██╗███╗   ██╗████████╗███████╗██╗██╗     ██╗      ██████╗  ██████╗ ███████╗\n"
        "  ██║████╗  ██║╚══██╔══╝██╔════╝██║██║     ██║      ██╔══██╗██╔═══██╗██╔════╝\n"
        "  ██║██╔██╗ ██║   ██║   █████╗  ██║██║     ██║      ██║  ██║██║   ██║███████╗\n"
        "  ██║██║╚██╗██║   ██║   ██╔══╝  ██║██║     ██║      ██║  ██║██║   ██║╚════██║\n"
        "  ██║██║ ╚████║   ██║   ███████╗██║███████╗██║      ██████╔╝╚██████╔╝███████║\n"
        "  ╚═╝╚═╝  ╚═══╝   ╚═╝   ╚══════╝╚═╝╚══════╝╚═╝      ╚═════╝  ╚═════╝ ╚══════╝"
    );

    vga_set_color(VGA_COLOR_WHITE, VGA_COLOR_BLACK);
    vga_putchar('\n');
    vga_set_color(VGA_COLOR_LIGHT_GREEN, VGA_COLOR_BLACK);
    vga_puts("          ");
    demo_type("the command line of the future", 55);
    vga_putchar('\n');

    vga_set_color(VGA_COLOR_DARK_GREY, VGA_COLOR_BLACK);
    vga_puts("\n  Developed by Inteilix Software Corporation");
    vga_puts("          Version 1.0\n");
    vga_set_color(VGA_COLOR_WHITE, VGA_COLOR_BLACK);

    /* Boot chime */
    speaker_beep(523, 80); speaker_off(); timer_sleep(40);
    speaker_beep(659, 80); speaker_off(); timer_sleep(40);
    speaker_beep(784, 120); speaker_off();

    demo_any_key();

    /* ── Feature 1: IntelliShell ─────────────────────────────────────── */
    vga_clear();
    demo_banner("  FEATURE 1 — IntelliShell with Natural Language Processing",
                VGA_COLOR_BLUE);
    vga_set_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK);
    println("  IntelliShell understands both commands and plain English.");
    println("  It supports 30+ built-in commands, command history (↑/↓),");
    println("  and a built-in NLP translator.");
    vga_putchar('\n');

    vga_set_color(VGA_COLOR_LIGHT_GREEN, VGA_COLOR_BLACK);
    demo_type("  C:\\> show files\n", 55);
    vga_set_color(VGA_COLOR_WHITE, VGA_COLOR_BLACK);
    println("   Directory of C:\\");
    println("    BIN       <DIR>");
    println("    APPS      <DIR>");
    println("    README.TXT     243");
    vga_putchar('\n');

    vga_set_color(VGA_COLOR_LIGHT_GREEN, VGA_COLOR_BLACK);
    demo_type("  C:\\> make a folder named PROJECTS\n", 55);
    vga_set_color(VGA_COLOR_WHITE, VGA_COLOR_BLACK);
    println("  Creating directory: PROJECTS");
    println("  Done.");
    vga_putchar('\n');

    vga_set_color(VGA_COLOR_LIGHT_GREEN, VGA_COLOR_BLACK);
    demo_type("  C:\\> how much memory do i have\n", 55);
    vga_set_color(VGA_COLOR_WHITE, VGA_COLOR_BLACK);
    println("  Memory Status");
    println("  =============");
    println("  Total  : 65536 KB (64 MB)");
    println("  Used   : 2048 KB");
    println("  Free   : 63488 KB");

    speaker_beep(660, 60); speaker_off();
    demo_any_key();

    /* ── Feature 2: Protected-mode kernel ───────────────────────────── */
    vga_clear();
    demo_banner("  FEATURE 2 — Bare-Metal Protected-Mode Kernel",
                VGA_COLOR_MAGENTA);
    vga_set_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK);
    println("  inteiliDOS runs ring-0 protected mode (IA-32) with no OS underneath.");
    println("  The kernel boots via Multiboot1 (GRUB2), sets up the GDT, IDT,");
    println("  remaps the PIC, and starts the PIT at 1 kHz before handing off");
    println("  to IntelliShell.");
    vga_putchar('\n');
    vga_set_color(VGA_COLOR_LIGHT_CYAN, VGA_COLOR_BLACK);
    println("  Subsystems active right now:");
    vga_set_color(VGA_COLOR_LIGHT_GREEN, VGA_COLOR_BLACK);
    println("    [OK]  GDT (Global Descriptor Table)       — flat 32-bit segments");
    println("    [OK]  IDT (Interrupt Descriptor Table)    — 48 vectors");
    println("    [OK]  PIC (8259A) remapped                — IRQs 0-15 at vectors 32-47");
    println("    [OK]  PIT 8254 timer                      — 1000 Hz tick");
    println("    [OK]  Physical memory manager             — bitmap allocator");
    println("    [OK]  Kernel heap (kmalloc/kfree)         — 2 MB");
    vga_set_color(VGA_COLOR_WHITE, VGA_COLOR_BLACK);
    vga_putchar('\n');
    vga_printf("  System uptime: %u ticks (~%u seconds)\n",
               timer_get_ticks(), timer_get_ticks() / 1000);

    speaker_beep(660, 60); speaker_off();
    demo_any_key();

    /* ── Feature 3: Hardware drivers ────────────────────────────────── */
    vga_clear();
    demo_banner("  FEATURE 3 — Hardware Drivers: PS/2, USB, PCI, ATA, ATAPI",
                VGA_COLOR_GREEN);
    vga_set_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK);
    println("  inteiliDOS includes drivers for the most common PC hardware,");
    println("  all written from scratch in C and NASM — no external libraries.\n");

    vga_set_color(VGA_COLOR_LIGHT_CYAN, VGA_COLOR_BLACK);
    println("  PS/2 Keyboard");
    vga_set_color(VGA_COLOR_WHITE, VGA_COLOR_BLACK);
    println("    IRQ 1 handler decodes scan-code set 2.  Both shifted and");
    println("    unshifted characters, arrow keys, F-keys, and special keys.\n");

    vga_set_color(VGA_COLOR_LIGHT_CYAN, VGA_COLOR_BLACK);
    println("  USB HID Keyboard (UHCI)");
    vga_set_color(VGA_COLOR_WHITE, VGA_COLOR_BLACK);
    println("    Detects UHCI host controllers via PCI config space, enumerates");
    println("    both root ports, and injects HID keystrokes into the shared");
    println("    keyboard buffer — feeding keyboard_getchar() like PS/2 does.\n");

    vga_set_color(VGA_COLOR_LIGHT_CYAN, VGA_COLOR_BLACK);
    println("  ATA/IDE + ATAPI CD-ROM");
    vga_set_color(VGA_COLOR_WHITE, VGA_COLOR_BLACK);
    println("    Enumerates all four IDE positions. ATAPI drives (CD-ROMs)");
    println("    are detected by signature and driven via SCSI PACKET commands.");
    vga_set_color(VGA_COLOR_LIGHT_GREEN, VGA_COLOR_BLACK);

    int ncd = cdrom_count();
    vga_printf("    %d CD-ROM drive(s) detected on this system.\n", ncd);
    vga_set_color(VGA_COLOR_WHITE, VGA_COLOR_BLACK);

    speaker_beep(660, 60); speaker_off();
    demo_any_key();

    /* ── Feature 4: IEdit ───────────────────────────────────────────── */
    vga_clear();
    demo_banner("  FEATURE 4 — IEdit Full-Screen Text Editor", VGA_COLOR_CYAN);
    vga_set_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK);
    println("  IEdit is a full-screen text editor built into the kernel.");
    println("  No external dependencies — VGA memory is accessed directly.\n");
    vga_set_color(VGA_COLOR_WHITE, VGA_COLOR_BLACK);
    println("  Layout:");
    println("    Row  0    Title bar  (filename + dirty flag)");
    println("    Rows 1-22 Editing area with line-number gutter");
    println("    Row  23   Status bar  (row, col, line count)");
    println("    Row  24   Message bar\n");
    println("  Key bindings:");
    vga_set_color(VGA_COLOR_LIGHT_CYAN, VGA_COLOR_BLACK);
    println("    Arrow keys     Move cursor");
    println("    Enter          Insert new line");
    println("    Backspace      Delete character before cursor");
    println("    Ctrl+K         Delete current line");
    println("    Ctrl+S         Save (in-memory; full FS pending ATA disk I/O)");
    println("    Ctrl+Q         Quit (prompts if unsaved)");
    vga_set_color(VGA_COLOR_WHITE, VGA_COLOR_BLACK);
    vga_putchar('\n');
    vga_set_color(VGA_COLOR_DARK_GREY, VGA_COLOR_BLACK);
    println("  Launch with:  IEDIT  or  IEDIT MYFILE.TXT");
    vga_set_color(VGA_COLOR_WHITE, VGA_COLOR_BLACK);

    speaker_beep(660, 60); speaker_off();
    demo_any_key();

    /* ── Feature 5: InteiliBASIC ────────────────────────────────────── */
    vga_clear();
    demo_banner("  FEATURE 5 — InteiliBASIC Interpreter", VGA_COLOR_BROWN);
    vga_set_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK);
    println("  A complete integer BASIC interpreter running in the kernel.");
    println("  Supports variables, loops, conditionals, subroutines, and more.\n");
    vga_set_color(VGA_COLOR_WHITE, VGA_COLOR_BLACK);

    vga_set_color(VGA_COLOR_DARK_GREY, VGA_COLOR_BLACK);
    println("  Example program:");
    vga_set_color(VGA_COLOR_LIGHT_GREEN, VGA_COLOR_BLACK);
    demo_type(
        "  10 PRINT \"Hello from InteiliBASIC!\"\n"
        "  20 FOR I = 1 TO 5\n"
        "  30   PRINT I * I\n"
        "  40 NEXT I\n"
        "  50 END\n",
        35);
    vga_set_color(VGA_COLOR_WHITE, VGA_COLOR_BLACK);
    vga_putchar('\n');
    vga_set_color(VGA_COLOR_DARK_GREY, VGA_COLOR_BLACK);
    println("  Supported statements: PRINT, LET, IF/THEN, FOR/NEXT,");
    println("  GOTO, GOSUB, RETURN, INPUT, REM, END, LIST, RUN, NEW");
    vga_set_color(VGA_COLOR_WHITE, VGA_COLOR_BLACK);
    vga_putchar('\n');
    vga_set_color(VGA_COLOR_DARK_GREY, VGA_COLOR_BLACK);
    println("  Launch with:  BASIC");
    vga_set_color(VGA_COLOR_WHITE, VGA_COLOR_BLACK);

    speaker_beep(660, 60); speaker_off();
    demo_any_key();

    /* ── Feature 6: InteiliSheets ───────────────────────────────────── */
    vga_clear();
    demo_banner("  FEATURE 6 — InteiliSheets Spreadsheet Application",
                VGA_COLOR_LIGHT_GREEN);
    vga_set_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK);
    println("  A full-screen spreadsheet application running entirely in the");
    println("  kernel — 7 columns (A–G), 50 rows, scrollable.\n");
    vga_set_color(VGA_COLOR_WHITE, VGA_COLOR_BLACK);
    println("  Supported formulas:");
    vga_set_color(VGA_COLOR_LIGHT_CYAN, VGA_COLOR_BLACK);
    println("    =SUM(A1:G10)   Sum a rectangular range of cells");
    println("    =AVG(A1:A10)   Average a range");
    vga_set_color(VGA_COLOR_WHITE, VGA_COLOR_BLACK);
    vga_putchar('\n');
    println("  Features:");
    println("    Arrow keys to navigate, Enter to edit, Del to clear");
    println("    Numbers right-aligned, text left-aligned");
    println("    Formula bar shows raw cell content");
    println("    Ctrl+S acknowledges save (full disk I/O pending)");
    println("    Ctrl+Q to quit back to IntelliShell");
    vga_putchar('\n');
    vga_set_color(VGA_COLOR_DARK_GREY, VGA_COLOR_BLACK);
    println("  Launch with:  SHEETS");
    vga_set_color(VGA_COLOR_WHITE, VGA_COLOR_BLACK);

    speaker_beep(660, 60); speaker_off();
    demo_any_key();

    /* ── Feature 7: InteiliTalk ─────────────────────────────────────── */
    vga_clear();
    demo_banner("  FEATURE 7 — InteiliTalk Text-to-Speech via PC Speaker",
                VGA_COLOR_LIGHT_MAGENTA);
    vga_set_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK);
    println("  InteiliTalk synthesises speech using the PC speaker — the");
    println("  same hardware that made those distinctive 1990s beeps.\n");
    vga_set_color(VGA_COLOR_WHITE, VGA_COLOR_BLACK);
    println("  How it works:");
    println("    Each character maps to a phoneme (frequency + duration).");
    println("    Common digraphs (TH, SH, CH, NG, PH...) are detected and");
    println("    given distinct phonemes before single-char fallback.");
    println("    A 10 ms articulation gap between phonemes improves clarity.\n");
    println("  Speaker API (already in kernel/timer.h):");
    vga_set_color(VGA_COLOR_LIGHT_CYAN, VGA_COLOR_BLACK);
    println("    speaker_on(freq_hz)              — continuous tone");
    println("    speaker_off()                    — silence");
    println("    speaker_beep(freq_hz, dur_ms)    — timed tone");
    vga_set_color(VGA_COLOR_WHITE, VGA_COLOR_BLACK);
    vga_putchar('\n');
    vga_set_color(VGA_COLOR_DARK_GREY, VGA_COLOR_BLACK);
    println("  Speak directly:  TALK Hello world");
    vga_set_color(VGA_COLOR_WHITE, VGA_COLOR_BLACK);
    vga_putchar('\n');
    println("  Demonstrating now...");
    timer_sleep(500);

    talk_speak("inteiliDOS: the command line of the future.");

    speaker_beep(660, 60); speaker_off();
    demo_any_key();

    /* ── Feature 8: TETRIS ──────────────────────────────────────────── */
    vga_clear();
    demo_banner("  FEATURE 8 — TETRIS: Classic Block-Stacking with Korobeiniki BGM",
                VGA_COLOR_RED);
    vga_set_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK);
    println("  The classic falling-block puzzle game, running bare-metal inside");
    println("  inteiliDOS.  Stack tetrominoes, clear lines, and rack up points");
    println("  while the iconic Korobeiniki melody plays through the PC speaker.\n");
    vga_set_color(VGA_COLOR_WHITE, VGA_COLOR_BLACK);
    println("  Controls: Arrow keys to move/rotate, Down to soft-drop,");
    println("            Space to hard-drop, Q to quit.\n");
    vga_set_color(VGA_COLOR_LIGHT_GREEN, VGA_COLOR_BLACK);
    demo_type("  C:\\> TETRIS\n", 55);
    vga_set_color(VGA_COLOR_WHITE, VGA_COLOR_BLACK);
    println("  *** TETRIS — inteiliDOS Edition ***\n");
    demo_tetris_preview();
    vga_set_color(VGA_COLOR_DARK_GREY, VGA_COLOR_BLACK);
    println("  Korobeiniki plays in the background.  Press Q to quit.");
    vga_set_color(VGA_COLOR_WHITE, VGA_COLOR_BLACK);

    speaker_beep(660, 60); speaker_off();
    demo_any_key();

    /* ── End screen ─────────────────────────────────────────────────── */
    vga_clear();
    vga_set_color(VGA_COLOR_LIGHT_CYAN, VGA_COLOR_BLACK);
    println(
        "\n\n"
        "   ╔══════════════════════════════════════════════════════════════╗\n"
        "   ║                                                              ║\n"
        "   ║          inteiliDOS  —  Version 1.0                         ║\n"
        "   ║          Developed by Inteilix Software Corporation          ║\n"
        "   ║                                                              ║\n"
        "   ║   \"The future still has a blinking cursor.\"                 ║\n"
        "   ║                                                              ║\n"
        "   ╚══════════════════════════════════════════════════════════════╝\n"
    );
    vga_set_color(VGA_COLOR_WHITE, VGA_COLOR_BLACK);
    println("\n  Features shown:");
    vga_set_color(VGA_COLOR_LIGHT_GREEN, VGA_COLOR_BLACK);
    println("    IntelliShell + NLP  •  Protected-Mode Kernel  •  Hardware Drivers");
    println("    IEdit Text Editor   •  InteiliBASIC            •  InteiliSheets");
    println("    InteiliTalk TTS     •  TETRIS (Korobeiniki BGM)");
    vga_set_color(VGA_COLOR_WHITE, VGA_COLOR_BLACK);
    vga_putchar('\n');
    println("  Type HELP to explore all commands.");
    vga_putchar('\n');

    /* Fanfare */
    speaker_beep(523, 100); speaker_off(); timer_sleep(30);
    speaker_beep(659, 100); speaker_off(); timer_sleep(30);
    speaker_beep(784, 100); speaker_off(); timer_sleep(30);
    speaker_beep(1047, 200); speaker_off();

    return 0;
}

/* SCRIPT -- run .ISH script */
static int cmd_script(int argc, const char *argv[]) {
    if (argc < 1) { println("Usage: SCRIPT <file.ish>"); return 1; }
    vga_printf("Running IntelliShell script: %s\n", argv[0]);
    println("(Script engine requires file system driver)");
    return 0;
}

/* SHUTDOWN / RESTART */
static int cmd_shutdown(int argc, const char *argv[]) {
    (void)argc; (void)argv;
    vga_set_color(VGA_COLOR_LIGHT_CYAN, VGA_COLOR_BLACK);
    println("\nSaving system state...");
    timer_sleep(300);
    println("Goodbye.");
    timer_sleep(200);
    println("Powering off...");
    vga_set_color(VGA_COLOR_WHITE, VGA_COLOR_BLACK);
    /* ACPI shutdown (port 0x604 for QEMU) */
    __asm__ volatile (
        "outw %0, %1"
        :
        : "a"((uint16_t)0x2000), "Nd"((uint16_t)0x604)
    );
    /* Bochs / older QEMU */
    for (;;) __asm__ volatile ("cli; hlt");
    return 0;
}
static int cmd_restart(int argc, const char *argv[]) {
    (void)argc; (void)argv;
    println("Rebooting...");
    timer_sleep(300);
    /* Pulse keyboard controller reset line (port 0x64, command 0xFE) */
    __asm__ volatile (
        "movb $0xFE, %%al\n\t"
        "outb %%al, $0x64\n\t"
        ::: "%eax"
    );
    for (;;) __asm__ volatile ("hlt");
    return 0;
}

/* FORMAT stub */
static int cmd_format(int argc, const char *argv[]) {
    if (argc < 1) { println("Usage: FORMAT <drive>"); return 1; }
    vga_set_color(VGA_COLOR_LIGHT_RED, VGA_COLOR_BLACK);
    vga_printf("WARNING: This will erase all data on %s\n", argv[0]);
    vga_set_color(VGA_COLOR_WHITE, VGA_COLOR_BLACK);
    println("Are you sure? (Y/N) [Feature requires disk driver]");
    return 0;
}

/* LABEL */
static int cmd_label(int argc, const char *argv[]) {
    if (argc < 1) { println("Usage: LABEL <name>"); return 1; }
    vga_printf("Setting volume label to: %s\n", argv[0]);
    println("Done.");
    return 0;
}

/* RUN -- run an executable */
static int cmd_run(int argc, const char *argv[]) {
    if (argc < 1) { println("Usage: RUN [/DOS] <program>"); return 1; }
    int dos_compat = (kstrcmp(argv[0], "/DOS") == 0 || kstrcmp(argv[0], "/dos") == 0);
    const char *prog = dos_compat && argc > 1 ? argv[1] : argv[0];
    if (dos_compat)
        vga_printf("Launching '%s' in DOS compatibility layer...\n", prog);
    else
        vga_printf("Launching '%s'...\n", prog);
    println("(Process management requires scheduler)");
    return 0;
}

/* DISKCHECK -- read back the installed MBR/VBR and report what version is on disk */
static int cmd_diskcheck(int argc, const char *argv[]) {
    (void)argc; (void)argv;

    static uint8_t buf[512];

    vga_set_color(VGA_COLOR_LIGHT_CYAN, VGA_COLOR_BLACK);
    vga_puts("inteiliDOS Disk Verification\n");
    vga_puts("----------------------------\n\n");
    vga_set_color(VGA_COLOR_WHITE, VGA_COLOR_BLACK);

    /* ── Step 1: Find a readable drive ─────────────────────────────────── */
    int drive = -1;
    for (int d = 0; d < 8; d++) {
        if (ata_read_sector((uint8_t)d, 0, buf) == 0) { drive = d; break; }
    }
    if (drive < 0) {
        vga_set_color(VGA_COLOR_LIGHT_RED, VGA_COLOR_BLACK);
        vga_puts("ERROR: Cannot read sector 0 from any drive.\n");
        vga_puts("       The ATA/AHCI driver found no readable disk.\n");
        vga_set_color(VGA_COLOR_WHITE, VGA_COLOR_BLACK);
        return -1;
    }
    vga_printf("Reading from drive index %d\n\n", drive);

    /* ── Step 2: MBR boot signature ────────────────────────────────────── */
    vga_puts("MBR (LBA 0): boot signature ");
    if (buf[0x1FE] == 0x55 && buf[0x1FF] == 0xAA) {
        vga_set_color(VGA_COLOR_LIGHT_GREEN, VGA_COLOR_BLACK);
        vga_puts("55 AA  OK\n");
    } else {
        vga_set_color(VGA_COLOR_LIGHT_RED, VGA_COLOR_BLACK);
        vga_printf("%02X %02X  MISSING — drive not bootable!\n",
                   (unsigned)buf[0x1FE], (unsigned)buf[0x1FF]);
    }
    vga_set_color(VGA_COLOR_WHITE, VGA_COLOR_BLACK);

    /* ── Step 3: Find inteiliDOS partition (type 0x99) ──────────────────── */
    uint32_t part_lba = 0;
    for (int i = 0; i < 4; i++) {
        int off = 0x1BE + i * 16;
        if (buf[off + 4] == 0x99) {
            part_lba = (uint32_t)buf[off+8]  | ((uint32_t)buf[off+9]  << 8)
                     | ((uint32_t)buf[off+10] << 16) | ((uint32_t)buf[off+11] << 24);
            vga_printf("inteiliDOS partition: entry %d, LBA %u\n", i, (unsigned)part_lba);
            break;
        }
    }
    if (part_lba == 0) {
        vga_set_color(VGA_COLOR_BROWN, VGA_COLOR_BLACK);
        vga_puts("WARNING: No inteiliDOS partition (type 0x99) in partition table.\n");
        vga_puts("         Setup has not been run, or this is not the right drive.\n");
        vga_set_color(VGA_COLOR_WHITE, VGA_COLOR_BLACK);
        return 0;
    }

    /* ── Step 4: Read VBR ───────────────────────────────────────────────── */
    if (ata_read_sector((uint8_t)drive, part_lba, buf) != 0) {
        vga_set_color(VGA_COLOR_LIGHT_RED, VGA_COLOR_BLACK);
        vga_puts("ERROR: Cannot read VBR sector.\n");
        vga_set_color(VGA_COLOR_WHITE, VGA_COLOR_BLACK);
        return -1;
    }

    vga_puts("VBR boot signature: ");
    if (buf[0x1FE] == 0x55 && buf[0x1FF] == 0xAA) {
        vga_set_color(VGA_COLOR_LIGHT_GREEN, VGA_COLOR_BLACK);
        vga_puts("55 AA  OK\n");
    } else {
        vga_set_color(VGA_COLOR_LIGHT_RED, VGA_COLOR_BLACK);
        vga_puts("MISSING\n");
    }
    vga_set_color(VGA_COLOR_WHITE, VGA_COLOR_BLACK);

    /* ── Step 5: Identify VBR version by key bytes ──────────────────────── */
    /* Offset 0x077 tells us the A20 method and whether print diags are present.
     * Offset 0x0C0-ish is the far-jump to Phase 2 (EA xx 7C 08 00).         */
    vga_puts("VBR version: ");
    uint8_t b77 = buf[0x77], b78 = buf[0x78];
    /* Find the far-jump to Phase 2 (EA xx 7C 08 00) around 0x0C0-0x0E0 */
    uint8_t phase2_lo = 0;
    for (int i = 0xB0; i < 0xE5; i++) {
        if (buf[i] == 0xEA && buf[i+2] == 0x7C && buf[i+3] == 0x08 && buf[i+4] == 0x00) {
            phase2_lo = buf[i+1]; break;
        }
    }
    if (b77 == 0x7C && b78 == 0x89 && phase2_lo == 0xBA) {
        vga_set_color(VGA_COLOR_LIGHT_GREEN, VGA_COLOR_BLACK);
        vga_puts("CURRENT (port-0x92 A20, single INT 10h '*', direct VGA mode-set)\n");
    } else if (b77 == 0xB0 && b78 == 0x41 && phase2_lo == 0xD1) {
        vga_set_color(VGA_COLOR_BROWN, VGA_COLOR_BLACK);
        vga_puts("PREVIOUS (port-0x92 A20 + INT 10h diagnostics)\n");
    } else if (b77 == 0xE4 && b78 == 0x92 && phase2_lo == 0xC5) {
        vga_set_color(VGA_COLOR_BROWN, VGA_COLOR_BLACK);
        vga_puts("OLD-2 (port-0x92 A20, no print diagnostics)\n");
    } else if (b77 == 0xE4 && b78 == 0x64) {
        vga_set_color(VGA_COLOR_LIGHT_RED, VGA_COLOR_BLACK);
        vga_puts("OLD-1 (KBC A20 — hangs after printing 'i' on this laptop!)\n");
    } else {
        vga_set_color(VGA_COLOR_DARK_GREY, VGA_COLOR_BLACK);
        vga_printf("UNKNOWN (byte 0x77=0x%02X 0x78=0x%02X phase2_lo=0x%02X)\n",
                   (unsigned)b77, (unsigned)b78, (unsigned)phase2_lo);
    }
    vga_set_color(VGA_COLOR_WHITE, VGA_COLOR_BLACK);

    /* ── Step 6: Kernel LBA patch ───────────────────────────────────────── */
    uint32_t klba = (uint32_t)buf[0x21] | ((uint32_t)buf[0x22]<<8)
                  | ((uint32_t)buf[0x23]<<16) | ((uint32_t)buf[0x24]<<24);
    vga_printf("Kernel LBA in VBR: %u  (should be %u)\n",
               (unsigned)klba, (unsigned)(part_lba + 1));
    if (klba == part_lba + 1) {
        vga_set_color(VGA_COLOR_LIGHT_GREEN, VGA_COLOR_BLACK);
        vga_puts("Kernel LBA: OK\n");
    } else if (klba == 0) {
        vga_set_color(VGA_COLOR_LIGHT_RED, VGA_COLOR_BLACK);
        vga_puts("Kernel LBA: ZERO — setup did not patch the VBR!\n");
    } else {
        vga_set_color(VGA_COLOR_BROWN, VGA_COLOR_BLACK);
        vga_puts("Kernel LBA: MISMATCH — partition may have moved since install.\n");
    }
    vga_set_color(VGA_COLOR_WHITE, VGA_COLOR_BLACK);

    vga_puts("\nTo update the VBR and kernel on disk, run: setup\n");
    return 0;
}

/* PROGRAM -- launch LaunchPad program loader */
static int cmd_program(int argc, const char *argv[]) {
    (void)argc; (void)argv;
    launchpad_run();
    return 0;
}

/* =========================================================================
 * NLP translator
 * ========================================================================= */
int nlp_translate(const char *input, char *out, int out_len) {
    /* Lower-case copy for matching */
    char lower[SHELL_LINE_MAX];
    kstrncpy(lower, input, SHELL_LINE_MAX - 1);
    lower[SHELL_LINE_MAX - 1] = '\0';
    kstrtolower(lower);

    /* Natural-language → canonical command mappings */
    struct { const char *pattern; const char *replacement; } nlp[] = {
        /* File listing */
        {"show files",          "DIR"},
        {"list files",          "DIR"},
        {"show directory",      "DIR"},
        {"list directory",      "DIR"},
        {"what's here",         "DIR"},
        {"what is here",        "DIR"},
        /* Location */
        {"where am i",          "CD"},
        {"current directory",   "CD"},
        {"pwd",                 "CD"},
        /* Clear */
        {"clear screen",        "CLS"},
        {"clear the screen",    "CLS"},
        /* Help */
        {"i need help",         "HELP"},
        {"what can i do",       "HELP"},
        {"show commands",       "HELP"},
        {"list commands",       "HELP"},
        /* Memory */
        {"how much memory",     "MEM"},
        {"show memory",         "MEM"},
        {"check memory",        "MEM"},
        /* System info */
        {"system info",         "SYSINFO"},
        {"show system",         "SYSINFO"},
        {"about this",          "ABOUT"},
        /* Shutdown */
        {"turn off",            "SHUTDOWN"},
        {"power off",           "SHUTDOWN"},
        {"shut down",           "SHUTDOWN"},
        {"shut it down",        "SHUTDOWN"},
        {"reboot",              "RESTART"},
        {"restart the computer","RESTART"},
        /* Quit / return to shell */
        {"quit",                "QUIT"},
        {"go back",             "QUIT"},
        {"return to shell",     "QUIT"},
        {NULL, NULL}
    };

    for (int i = 0; nlp[i].pattern; i++) {
        if (kstrstr(lower, nlp[i].pattern)) {
            kstrncpy(out, nlp[i].replacement, (size_t)out_len - 1);
            out[out_len - 1] = '\0';
            return 1;
        }
    }

    /* "make a folder named X" -> "MKDIR X" */
    const char *p;
    if ((p = kstrstr(lower, "make a folder named ")) ||
        (p = kstrstr(lower, "create a folder named ")) ||
        (p = kstrstr(lower, "make folder named ")) ||
        (p = kstrstr(lower, "create folder named "))) {
        /* Skip past "named " */
        const char *name = kstrstr(p, "named ");
        if (name) {
            name += 6;
            /* Reconstruct uppercase name from original input */
            size_t offset = (size_t)(name - lower);
            char upper_name[64];
            kstrncpy(upper_name, input + offset, 63);
            upper_name[63] = '\0';
            for (char *c = upper_name; *c; c++) if (*c >= 'a' && *c <= 'z') *c -= 32;
            /* Strip trailing whitespace */
            int l = (int)kstrlen(upper_name);
            while (l > 0 && (upper_name[l-1] == ' ' || upper_name[l-1] == '\n')) upper_name[--l] = '\0';
            /* Build output */
            kstrncpy(out, "MKDIR ", (size_t)out_len - 1);
            kstrcat(out, upper_name);
            return 1;
        }
    }

    /* "remove X" / "delete X" -> "DELETE X" */
    if (kstrncmp(lower, "remove ", 7) == 0) {
        kstrncpy(out, "DELETE ", (size_t)out_len - 1);
        /* uppercase rest from original */
        char rest[64];
        kstrncpy(rest, input + 7, 63);
        for (char *c = rest; *c; c++) if (*c >= 'a' && *c <= 'z') *c -= 32;
        kstrcat(out, rest);
        return 1;
    }

    /* "install X" — already handled if typed, but also match "download X" */
    if (kstrncmp(lower, "download ", 9) == 0) {
        kstrncpy(out, "INSTALL ", (size_t)out_len - 1);
        char rest[64];
        kstrncpy(rest, input + 9, 63);
        for (char *c = rest; *c; c++) if (*c >= 'a' && *c <= 'z') *c -= 32;
        kstrcat(out, rest);
        return 1;
    }

    /* "go to X" / "go to folder X" -> "CD X" */
    if (kstrncmp(lower, "go to ", 6) == 0) {
        const char *dest = lower + 6;
        if (kstrncmp(dest, "folder ", 7) == 0) dest += 7;
        kstrncpy(out, "CD ", (size_t)out_len - 1);
        char upper_dest[64];
        kstrncpy(upper_dest, dest, 63);
        for (char *c = upper_dest; *c; c++) if (*c >= 'a' && *c <= 'z') *c -= 32;
        kstrcat(out, upper_dest);
        return 1;
    }

    return 0;   /* no translation */
}

/* =========================================================================
 * Command dispatcher
 * ========================================================================= */
int command_dispatch(const char *cmd, int argc, const char *argv[]) {
    if (kstrcmp(cmd, "DIR")      == 0) return cmd_dir(argc, argv);
    if (kstrcmp(cmd, "LS")       == 0) return cmd_dir(argc, argv);
    if (kstrcmp(cmd, "CD")       == 0) return cmd_cd(argc, argv);
    if (kstrcmp(cmd, "CHDIR")    == 0) return cmd_cd(argc, argv);
    if (kstrcmp(cmd, "CLS")      == 0) return cmd_cls(argc, argv);
    if (kstrcmp(cmd, "CLEAR")    == 0) return cmd_cls(argc, argv);
    if (kstrcmp(cmd, "HELP")     == 0 || kstrcmp(cmd, "?") == 0)
                                        return cmd_help(argc, argv);
    if (kstrcmp(cmd, "MEM")      == 0) return cmd_mem(argc, argv);
    if (kstrcmp(cmd, "SYSINFO")  == 0) return cmd_sysinfo(argc, argv);
    if (kstrcmp(cmd, "ABOUT")    == 0) return cmd_about(argc, argv);
    if (kstrcmp(cmd, "HELLO")    == 0) return cmd_hello(argc, argv);
    if (kstrcmp(cmd, "SETUP")      == 0 ||
        kstrcmp(cmd, "INSTALL-OS") == 0) return cmd_setup(argc, argv);
    if (kstrcmp(cmd, "FM")         == 0 ||
        kstrcmp(cmd, "FILEMAN")    == 0 ||
        kstrcmp(cmd, "FILEMANAGER")== 0) return cmd_filemanager(argc, argv);
    if (kstrcmp(cmd, "TYPE")     == 0) return cmd_type(argc, argv);
    if (kstrcmp(cmd, "MKDIR")    == 0 || kstrcmp(cmd, "MD") == 0)
                                        return cmd_mkdir(argc, argv);
    if (kstrcmp(cmd, "COPY")     == 0) return cmd_copy(argc, argv);
    if (kstrcmp(cmd, "MOVE")     == 0) return cmd_move(argc, argv);
    if (kstrcmp(cmd, "DELETE")   == 0 || kstrcmp(cmd, "DEL") == 0
                                       || kstrcmp(cmd, "ERASE") == 0)
                                        return cmd_delete(argc, argv);
    if (kstrcmp(cmd, "TREE")     == 0) return cmd_tree(argc, argv);
    if (kstrcmp(cmd, "HISTORY")  == 0) return cmd_history(argc, argv);
    if (kstrcmp(cmd, "TIME")     == 0) return cmd_time(argc, argv);
    if (kstrcmp(cmd, "DATE")     == 0) return cmd_date(argc, argv);
    if (kstrcmp(cmd, "INSTALL")  == 0) return cmd_install(argc, argv);
    if (kstrcmp(cmd, "REMOVE")   == 0) return cmd_remove(argc, argv);
    if (kstrcmp(cmd, "UPDATE")   == 0) return cmd_update(argc, argv);
    if (kstrcmp(cmd, "SEARCH")   == 0) return cmd_search(argc, argv);
    if (kstrcmp(cmd, "BACKUP")   == 0) return cmd_backup(argc, argv);
    if (kstrcmp(cmd, "RESTORE")  == 0) return cmd_restore(argc, argv);
    if (kstrcmp(cmd, "TETRIS")   == 0) return cmd_tetris(argc, argv);
    if (kstrcmp(cmd, "IEDIT")    == 0 || kstrcmp(cmd, "EDIT") == 0)
                                        return cmd_iedit(argc, argv);
    if (kstrcmp(cmd, "BASIC")    == 0 || kstrcmp(cmd, "IBASIC") == 0)
                                        return cmd_basic(argc, argv);
    if (kstrcmp(cmd, "QUIT")     == 0 || kstrcmp(cmd, "EXIT") == 0)
                                        return cmd_quit(argc, argv);
    if (kstrcmp(cmd, "SHEETS")   == 0 || kstrcmp(cmd, "ISHEETS") == 0)
                                        return cmd_sheets(argc, argv);
    if (kstrcmp(cmd, "ALIVE")    == 0) return cmd_stillalive(argc, argv);
    if (kstrcmp(cmd, "DAISY")    == 0) return cmd_daisy(argc, argv);
    if (kstrcmp(cmd, "VOLUME")   == 0 || kstrcmp(cmd, "VOL")   == 0)
                                        return cmd_volume(argc, argv);
    if (kstrcmp(cmd, "TALK")     == 0 || kstrcmp(cmd, "ITALK") == 0)
                                        return cmd_talk(argc, argv);
    if (kstrcmp(cmd, "DEMO")     == 0) return cmd_demo(argc, argv);
    if (kstrcmp(cmd, "SCRIPT")   == 0) return cmd_script(argc, argv);
    if (kstrcmp(cmd, "SHUTDOWN") == 0) return cmd_shutdown(argc, argv);
    if (kstrcmp(cmd, "RESTART")  == 0 || kstrcmp(cmd, "REBOOT") == 0)
                                        return cmd_restart(argc, argv);
    if (kstrcmp(cmd, "DISKCHECK") == 0 || kstrcmp(cmd, "CHKDSK") == 0)
                                        return cmd_diskcheck(argc, argv);
    if (kstrcmp(cmd, "PROGRAM")  == 0 || kstrcmp(cmd, "LAUNCHPAD") == 0)
                                        return cmd_program(argc, argv);
    if (kstrcmp(cmd, "FORMAT")   == 0) return cmd_format(argc, argv);
    if (kstrcmp(cmd, "LABEL")    == 0) return cmd_label(argc, argv);
    if (kstrcmp(cmd, "RUN")      == 0) return cmd_run(argc, argv);

    /* Unknown */
    vga_set_color(VGA_COLOR_LIGHT_RED, VGA_COLOR_BLACK);
    vga_printf("'%s' is not recognized as a command.\n", cmd);
    vga_set_color(VGA_COLOR_WHITE, VGA_COLOR_BLACK);
    println("Type HELP for a list of commands.");
    return 127;
}
