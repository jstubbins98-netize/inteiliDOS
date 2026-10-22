#ifndef MSDOS_STARTUP_H
#define MSDOS_STARTUP_H
#include "../fs.h"
#define MSDOS_BLASTER_LINE "SET BLASTER=A220 I5 D1 T3"
#define MSDOS_BLASTER16_LINE "SET BLASTER=A220 I5 D1 H5 T6"
/* Update only the emulator-created FAT16 disk. Returns zero on success.
 * Never edits user-supplied floppy/CD/USB images. */
int msdos_startup_sound(fs_image_t *disk);
int msdos_startup_sound_for(fs_image_t *disk,int sb16);
#endif
