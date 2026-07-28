/*
 * inteilidOS -- shell/commands.c
 * Built-in commands + IntelliShell NLP translator
 */

#include "commands.h"
#include "shell.h"
#include "iedit.h"
#include "tour.h"
#include "basic.h"
#include "sheets.h"
#include "talk.h"
#include "filemanager.h"
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
    println("  CDROM    - List CD-ROM drives and disc info");
    println("  CDROM EJECT [n] - Eject disc tray on drive n (default 0)");
    println("  CDROM INFO  [n] - Show capacity details for drive n");

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
    println("  TOUR     - A Tour of inteiliDOS and Your Computer");
    println("             (text adventure -- you've been shrunk into the PC!)");

    /* Extras */
    vga_set_color(VGA_COLOR_BROWN, VGA_COLOR_BLACK);
    println("\n  -- Extras ---------------------");
    vga_set_color(VGA_COLOR_LIGHT_CYAN, VGA_COLOR_BLACK);
    println("  ABOUT    - About inteiliDOS");
    println("  HELLO    - Say hello");
    println("  FM       - InteiliFile Manager (graphical file browser)");

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

/* TOUR -- text-adventure game */
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

    /* ── Feature 8: TOUR text adventure ────────────────────────────── */
    vga_clear();
    demo_banner("  FEATURE 8 — TOUR: A Text Adventure Inside Your PC",
                VGA_COLOR_RED);
    vga_set_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK);
    println("  You have been shrunk to the size of a transistor and inserted");
    println("  into the computer.  Navigate the CPU, RAM, hard disk, and more");
    println("  in this interactive text adventure built into inteiliDOS.\n");
    vga_set_color(VGA_COLOR_WHITE, VGA_COLOR_BLACK);
    println("  Location choices: CPU Die, Memory Bus, RAM Chip, IDE Controller,");
    println("                    VGA Adapter, PCI Bus, Power Supply.\n");
    vga_set_color(VGA_COLOR_LIGHT_GREEN, VGA_COLOR_BLACK);
    demo_type("  C:\\> TOUR\n", 55);
    vga_set_color(VGA_COLOR_WHITE, VGA_COLOR_BLACK);
    println("  *** A Tour of inteiliDOS and Your Computer ***");
    println("  You find yourself standing on the motherboard...");
    println("  The CPU hums above you.  The RAM towers loom in the distance.");
    vga_set_color(VGA_COLOR_DARK_GREY, VGA_COLOR_BLACK);
    println("\n  Available commands: GO, LOOK, EXAMINE, TAKE, INVENTORY, QUIT");
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
    println("    InteiliTalk TTS     •  TOUR Text Adventure");
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

/* CDROM -- CD-ROM drive control and information */
static int cmd_cdrom(int argc, const char *argv[]) {
    const cdrom_drive_t *drives = cdrom_drives();
    int n = cdrom_count();

    /* ---- CDROM EJECT [drive] ---- */
    if (argc >= 1 && (kstrcmp(argv[0], "EJECT") == 0 ||
                      kstrcmp(argv[0], "eject") == 0)) {
        uint8_t idx = 0;
        if (argc >= 2) {
            /* accept a drive index 0-3 */
            char c = argv[1][0];
            if (c >= '0' && c <= '3') idx = (uint8_t)(c - '0');
        }
        if (idx >= CDROM_MAX_DRIVES || drives[idx].present == CDROM_NOT_PRESENT) {
            vga_set_color(VGA_COLOR_LIGHT_RED, VGA_COLOR_BLACK);
            vga_printf("  No CD-ROM drive at index %d.\n", (int)idx);
            vga_set_color(VGA_COLOR_WHITE, VGA_COLOR_BLACK);
            return 1;
        }
        vga_printf("  Ejecting drive %d...\n", (int)idx);
        if (cdrom_eject(idx) == 0) {
            vga_set_color(VGA_COLOR_LIGHT_GREEN, VGA_COLOR_BLACK);
            vga_puts("  Tray ejected.\n");
        } else {
            vga_set_color(VGA_COLOR_LIGHT_RED, VGA_COLOR_BLACK);
            vga_puts("  Eject command rejected (drive may not support software eject).\n");
        }
        vga_set_color(VGA_COLOR_WHITE, VGA_COLOR_BLACK);
        return 0;
    }

    /* ---- CDROM INFO [drive] ---- */
    if (argc >= 1 && (kstrcmp(argv[0], "INFO") == 0 ||
                      kstrcmp(argv[0], "info") == 0)) {
        uint8_t idx = 0;
        if (argc >= 2) {
            char c = argv[1][0];
            if (c >= '0' && c <= '3') idx = (uint8_t)(c - '0');
        }
        if (idx >= CDROM_MAX_DRIVES || drives[idx].present == CDROM_NOT_PRESENT) {
            vga_set_color(VGA_COLOR_LIGHT_RED, VGA_COLOR_BLACK);
            vga_printf("  No CD-ROM drive at index %d.\n", (int)idx);
            vga_set_color(VGA_COLOR_WHITE, VGA_COLOR_BLACK);
            return 1;
        }
        const cdrom_drive_t *d = &drives[idx];
        vga_set_color(VGA_COLOR_LIGHT_CYAN, VGA_COLOR_BLACK);
        vga_printf("\n  CD-ROM Drive %d\n", (int)idx);
        vga_puts("  ==============\n");
        vga_set_color(VGA_COLOR_WHITE, VGA_COLOR_BLACK);
        const char *chan  = (idx < 2) ? "Primary"   : "Secondary";
        const char *pos   = (idx & 1) ? "Slave"     : "Master";
        vga_printf("  Position   : %s %s (IDE index %d)\n", chan, pos, (int)idx);
        if (d->last_lba == 0) {
            vga_puts("  Disc       : No disc inserted (or READ CAPACITY failed)\n");
        } else {
            uint32_t total_mb = (d->last_lba + 1) / (1048576u / d->block_size);
            vga_printf("  Last LBA   : %u\n", d->last_lba);
            vga_printf("  Block size : %u bytes\n", d->block_size);
            vga_printf("  Capacity   : ~%u MB (%u sectors)\n",
                       total_mb, d->last_lba + 1);
        }
        return 0;
    }

    /* ---- CDROM (no arguments) — list all drives ---- */
    vga_set_color(VGA_COLOR_LIGHT_CYAN, VGA_COLOR_BLACK);
    vga_puts("\n  CD-ROM Drives\n");
    vga_puts("  =============\n");
    vga_set_color(VGA_COLOR_WHITE, VGA_COLOR_BLACK);

    if (n == 0) {
        vga_set_color(VGA_COLOR_DARK_GREY, VGA_COLOR_BLACK);
        vga_puts("  No ATAPI CD-ROM drives detected.\n");
        vga_set_color(VGA_COLOR_WHITE, VGA_COLOR_BLACK);
        return 0;
    }

    for (int i = 0; i < CDROM_MAX_DRIVES; i++) {
        if (drives[i].present != CDROM_PRESENT) continue;
        const char *chan = (i < 2) ? "Primary" : "Secondary";
        const char *pos  = (i & 1) ? "Slave"   : "Master";
        vga_set_color(VGA_COLOR_LIGHT_GREEN, VGA_COLOR_BLACK);
        vga_printf("  Drive %d  ", i);
        vga_set_color(VGA_COLOR_WHITE, VGA_COLOR_BLACK);
        vga_printf("%s %s  —  ", chan, pos);
        if (drives[i].last_lba == 0) {
            vga_set_color(VGA_COLOR_DARK_GREY, VGA_COLOR_BLACK);
            vga_puts("no disc\n");
        } else {
            uint32_t sects = drives[i].last_lba + 1;
            uint32_t mb    = sects / (1048576u / drives[i].block_size);
            vga_printf("%u MB  (%u blocks x %u bytes)\n",
                       mb, sects, drives[i].block_size);
        }
    }
    vga_set_color(VGA_COLOR_DARK_GREY, VGA_COLOR_BLACK);
    vga_puts("\n  Sub-commands: CDROM EJECT [drive]   CDROM INFO [drive]\n");
    vga_set_color(VGA_COLOR_WHITE, VGA_COLOR_BLACK);
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
    if (kstrcmp(cmd, "TOUR")     == 0) return cmd_tour(argc, argv);
    if (kstrcmp(cmd, "IEDIT")    == 0 || kstrcmp(cmd, "EDIT") == 0)
                                        return cmd_iedit(argc, argv);
    if (kstrcmp(cmd, "BASIC")    == 0 || kstrcmp(cmd, "IBASIC") == 0)
                                        return cmd_basic(argc, argv);
    if (kstrcmp(cmd, "QUIT")     == 0 || kstrcmp(cmd, "EXIT") == 0)
                                        return cmd_quit(argc, argv);
    if (kstrcmp(cmd, "SHEETS")   == 0 || kstrcmp(cmd, "ISHEETS") == 0)
                                        return cmd_sheets(argc, argv);
    if (kstrcmp(cmd, "VOLUME")   == 0 || kstrcmp(cmd, "VOL")   == 0)
                                        return cmd_volume(argc, argv);
    if (kstrcmp(cmd, "TALK")     == 0 || kstrcmp(cmd, "ITALK") == 0)
                                        return cmd_talk(argc, argv);
    if (kstrcmp(cmd, "DEMO")     == 0) return cmd_demo(argc, argv);
    if (kstrcmp(cmd, "SCRIPT")   == 0) return cmd_script(argc, argv);
    if (kstrcmp(cmd, "SHUTDOWN") == 0) return cmd_shutdown(argc, argv);
    if (kstrcmp(cmd, "RESTART")  == 0 || kstrcmp(cmd, "REBOOT") == 0)
                                        return cmd_restart(argc, argv);
    if (kstrcmp(cmd, "CDROM")    == 0 || kstrcmp(cmd, "CD-ROM") == 0)
                                        return cmd_cdrom(argc, argv);
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
