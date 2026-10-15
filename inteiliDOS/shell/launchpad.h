/*
 * inteiliDOS -- shell/launchpad.h
 * LaunchPad 1.0 — load programs from CD-ROM or floppy into RAM
 */

#ifndef LAUNCHPAD_H
#define LAUNCHPAD_H
#include <stdint.h>

/* Launch the LaunchPad application.  Returns when the user presses Esc. */
void launchpad_run(void);
/* CDROM /RUN: CD-only browser; folders and native ELF programs only. */
void launchpad_run_cdrom(uint8_t drive);

#endif /* LAUNCHPAD_H */
