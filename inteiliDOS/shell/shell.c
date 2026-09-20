/*
 * inteilidOS -- shell/shell.c
 * IntelliShell -- main read-eval-print loop
 */

#include "shell.h"
#include "commands.h"
#include "../kernel/vga.h"
#include "../kernel/keyboard.h"
#include "../kernel/memory.h"
#include <stdint.h>

/* Shared state */
char cwd[256] = "C:\\";
char history[HISTORY_MAX][SHELL_LINE_MAX];
int  history_count = 0;

/* ---- History ---- */
static void history_push(const char *line) {
    if (history_count == HISTORY_MAX) {
        for (int i = 0; i < HISTORY_MAX - 1; i++)
            kstrcpy(history[i], history[i + 1]);
        history_count--;
    }
    kstrncpy(history[history_count], line, SHELL_LINE_MAX - 1);
    history[history_count][SHELL_LINE_MAX - 1] = '\0';
    history_count++;
}

/* ---- Tokenise a command line ---- */
static int tokenise(char *line, char *tokens[], int max_tokens) {
    int count = 0;
    char *p = line;
    while (*p && count < max_tokens) {
        while (*p == ' ') p++;
        if (!*p) break;
        tokens[count++] = p;
        while (*p && *p != ' ') p++;
        if (*p) *p++ = '\0';
    }
    return count;
}

/* ---- Read one line from keyboard ---- */
static void readline(char *buf, int max) {
    int pos = 0;
    int hist_idx = history_count;

    while (1) {
        int c = keyboard_getchar();

        if (c == KEY_ENTER || c == '\n' || c == '\r') {
            buf[pos] = '\0';
            vga_putchar('\n');
            return;
        }
        if (c == KEY_BACKSPACE || c == '\b') {
            if (pos > 0) {
                pos--;
                vga_putchar('\b');
                vga_putchar(' ');
                vga_putchar('\b');
            }
            continue;
        }
        if (c == KEY_UP) {
            /* Navigate history backwards */
            if (hist_idx > 0) {
                hist_idx--;
                /* Clear current line */
                while (pos > 0) {
                    vga_putchar('\b'); vga_putchar(' '); vga_putchar('\b');
                    pos--;
                }
                kstrncpy(buf, history[hist_idx], (size_t)(max - 1));
                pos = (int)kstrlen(buf);
                vga_puts(buf);
            }
            continue;
        }
        if (c == KEY_DOWN) {
            if (hist_idx < history_count) {
                hist_idx++;
                while (pos > 0) {
                    vga_putchar('\b'); vga_putchar(' '); vga_putchar('\b');
                    pos--;
                }
                if (hist_idx < history_count) {
                    kstrncpy(buf, history[hist_idx], (size_t)(max - 1));
                } else {
                    buf[0] = '\0';
                }
                pos = (int)kstrlen(buf);
                vga_puts(buf);
            }
            continue;
        }
        if (c == KEY_ESCAPE) {
            /* Clear line */
            while (pos > 0) {
                vga_putchar('\b'); vga_putchar(' '); vga_putchar('\b');
                pos--;
            }
            continue;
        }
        if (c >= 0x20 && c < 0x7F && pos < max - 1) {
            buf[pos++] = (char)c;
            vga_putchar((char)c);
        }
    }
}

/* ---- Print prompt ---- */
void shell_print_prompt(void) {
    vga_set_color(VGA_COLOR_LIGHT_GREEN, VGA_COLOR_BLACK);
    vga_puts(cwd);
    vga_set_color(VGA_COLOR_WHITE, VGA_COLOR_BLACK);
    vga_puts("> ");
}

/* ---- Main shell loop ---- */
void shell_run(void) {
    char line[SHELL_LINE_MAX];
    char translated[SHELL_LINE_MAX];

    while (1) {
        shell_print_prompt();
        readline(line, SHELL_LINE_MAX);

        /* Skip empty lines */
        int len = (int)kstrlen(line);
        while (len > 0 && (line[len-1] == ' ' || line[len-1] == '\t')) line[--len] = '\0';
        if (len == 0) continue;

        /* Record in history */
        history_push(line);

        /* ------------------------------------------------------------------
         * NLP translation pass:
         * If the line matches a natural-language pattern, replace it with
         * the canonical command silently.
         * ------------------------------------------------------------------ */
        char *effective = line;
        if (nlp_translate(line, translated, SHELL_LINE_MAX)) {
            effective = translated;
        }

        /* Make a mutable copy for tokenisation */
        char work[SHELL_LINE_MAX];
        kstrncpy(work, effective, SHELL_LINE_MAX - 1);
        work[SHELL_LINE_MAX - 1] = '\0';

        /* Tokenise */
        char *tokens[SHELL_ARG_MAX + 1];
        int   ntok = tokenise(work, tokens, SHELL_ARG_MAX + 1);
        if (ntok == 0) continue;

        /* Upper-case command word */
        for (char *p = tokens[0]; *p; p++)
            if (*p >= 'a' && *p <= 'z') *p -= 32;

        /* Dispatch */
        command_dispatch(tokens[0], ntok - 1, (const char **)tokens + 1);
    }
}
