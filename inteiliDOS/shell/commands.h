#ifndef COMMANDS_H
#define COMMANDS_H

/* Dispatch a parsed command line.
 * cmd  = command word (upper-cased)
 * argc = number of arguments (excluding cmd)
 * argv = argument strings
 * Returns 0 on success, non-zero on error/unknown command.
 */
int command_dispatch(const char *cmd, int argc, const char *argv[]);

/* Natural-language → canonical command translation.
 * Input:  raw line as typed
 * Output: translated canonical line (written into out, max out_len bytes)
 * Returns 1 if a translation was made, 0 if the line is already a command.
 */
int nlp_translate(const char *input, char *out, int out_len);

#endif /* COMMANDS_H */
