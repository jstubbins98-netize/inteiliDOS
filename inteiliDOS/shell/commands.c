/*
 * inteilidOS -- shell/commands.c
 * Built-in commands + IntelliShell NLP translator
 */

#include "commands.h"
#include "shell.h"
#include "iedit.h"
#include "tour.h"
#include "basic.h"
#include "../kernel/vga.h"
#include "../kernel/memory.h"
#include "../kernel/timer.h"
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
    println("  IEDIT    - Text editor");
    println("  BASIC    - InteiliBASIC interpreter");
    println("  NETWORK  - Network utilities");
    println("  PING     - Ping a host");

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
    println("  COFFEE   - Coffee module");

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

/* COFFEE easter egg */
static int cmd_coffee(int argc, const char *argv[]) {
    (void)argc; (void)argv;
    vga_set_color(VGA_COLOR_LIGHT_RED, VGA_COLOR_BLACK);
    println("Error:");
    vga_set_color(VGA_COLOR_WHITE, VGA_COLOR_BLACK);
    println("Coffee module not installed.");
    println("Install caffeine before continuing.");
    return 1;
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

/* NETWORK */
static int cmd_network(int argc, const char *argv[]) {
    (void)argc; (void)argv;
    println("Network commands: PING, FTP, HTTPGET, CHAT, SSH, TELNET, SYNC");
    println("(TCP/IP stack requires NIC driver)");
    return 0;
}
static int cmd_ping(int argc, const char *argv[]) {
    if (argc < 1) { println("Usage: PING <host>"); return 1; }
    vga_printf("Pinging %s...\n", argv[0]);
    for (int i = 0; i < 4; i++) {
        timer_sleep(250);
        vga_printf("Reply from %s: time=<NIC required>\n", argv[0]);
    }
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
    if (kstrcmp(cmd, "COFFEE")   == 0) return cmd_coffee(argc, argv);
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
    if (kstrcmp(cmd, "NETWORK")  == 0) return cmd_network(argc, argv);
    if (kstrcmp(cmd, "PING")     == 0) return cmd_ping(argc, argv);
    if (kstrcmp(cmd, "SCRIPT")   == 0) return cmd_script(argc, argv);
    if (kstrcmp(cmd, "SHUTDOWN") == 0) return cmd_shutdown(argc, argv);
    if (kstrcmp(cmd, "RESTART")  == 0 || kstrcmp(cmd, "REBOOT") == 0)
                                        return cmd_restart(argc, argv);
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
