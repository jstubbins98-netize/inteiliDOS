#ifndef SHELL_H
#define SHELL_H

/* Maximum line / token sizes */
#define SHELL_LINE_MAX   256
#define SHELL_TOKEN_MAX  64
#define SHELL_ARG_MAX    16
#define HISTORY_MAX      32

/* IntelliShell — main entry point (never returns) */
void shell_run(void);

/* Print the current prompt */
void shell_print_prompt(void);

/* Current working directory (shared with commands) */
extern char cwd[256];

/* History */
extern char history[HISTORY_MAX][SHELL_LINE_MAX];
extern int  history_count;

#endif /* SHELL_H */
