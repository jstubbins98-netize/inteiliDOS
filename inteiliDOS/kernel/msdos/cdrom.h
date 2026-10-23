#ifndef MSDOS_CDROM_H
#define MSDOS_CDROM_H
#include <stdint.h>
/* Read-only, streamed ISO -> DOS FAT16 view. No source files are copied. */
int msdos_cdrom_open(void);
void msdos_cdrom_close(void);
uint32_t msdos_cdrom_sectors(void);
int msdos_cdrom_read(uint32_t sector, uint8_t *out);
const char *msdos_cdrom_error(void);
#endif
