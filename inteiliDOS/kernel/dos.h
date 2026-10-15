#ifndef INTEILIDOS_DOS_H
#define INTEILIDOS_DOS_H
#include <stdint.h>
#include "isr.h"

/* Read-only filesystem bridge. Slots 0..7 correspond to DOS handles 5..12.
 * open returns 0, DOS error 2 (not found), 5 (denied), or 30 (I/O error).
 * read returns bytes, or a negative DOS error. Calls use the selected medium
 * and directory; the guest cannot select other physical devices. */
typedef struct {
    int (*open)(const char *path, unsigned slot, uint32_t *size);
    int32_t (*read)(unsigned slot, uint32_t offset, uint8_t *buf, uint32_t count);
    char drive; /* A for floppy, D for CD-ROM */
} dos_filesystem_t;

/* COM has no signature; callers must explicitly identify a .COM filename.
 * Returns 0 on ordinary termination (exit code via dos_exit_code()),
 * -1 malformed image, -2 unsupported service/instruction, -3 guest fault,
 * -4 user abort, -5 incompatible host paging. */
int dos_exec(const uint8_t *image, uint32_t size, int is_com,
             const dos_filesystem_t *fs);
int dos_exception(registers_t *r);
void dos_irq(registers_t *r);
uint8_t dos_exit_code(void);
const char *dos_error(void);
#endif
