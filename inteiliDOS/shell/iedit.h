#ifndef IEDIT_H
#define IEDIT_H

/*
 * iedit_run() -- launch the iEdit full-screen text editor.
 * fname may be NULL or empty for an unnamed buffer.
 * Returns when the user quits (Ctrl+Q).
 */
void iedit_run(const char *fname);

#endif /* IEDIT_H */
