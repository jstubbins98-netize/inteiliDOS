/*
 * inteiliDOS -- kernel/iso9660.h
 * Minimal ISO 9660 filesystem reader (CD-ROM)
 *
 * Reads the root directory and file data from a Mode 1 / Mode 2 Form 1
 * disc using cdrom_read_sector() for all low-level I/O.
 * No Rock Ridge, no Joliet, no path table — root directory only.
 */

#ifndef ISO9660_H
#define ISO9660_H

#include <stdint.h>

#define ISO9660_MAX_FILES 64
#define ISO9660_NAME_MAX  32   /* max filename length stored (incl. NUL) */

typedef struct {
    char     name[ISO9660_NAME_MAX]; /* filename, version suffix stripped  */
    uint32_t lba;                    /* start sector on disc               */
    uint32_t size;                   /* file size in bytes                 */
    uint8_t  is_dir;                 /* 1 = directory entry                */
} iso9660_dirent_t;

/*
 * iso9660_read_dir — read the root directory of the disc in drive_index.
 * Populates out[0..n-1] and returns n, or -1 on error (no disc, bad PVD).
 */
int iso9660_read_dir(uint8_t drive_index,
                     iso9660_dirent_t out[ISO9660_MAX_FILES]);

/*
 * iso9660_read_dir_at — read an arbitrary directory by LBA.
 *
 * Use this to navigate into subdirectories: pass the lba and size fields
 * from the parent directory's iso9660_dirent_t for the directory entry.
 *   drive_index : 0–3
 *   dir_lba     : start sector of the directory extent
 *   dir_size    : byte length of the directory extent
 * Returns entry count (>= 0), or -1 on I/O error.
 * "." and ".." entries are skipped automatically.
 */
int iso9660_read_dir_at(uint8_t drive_index,
                         uint32_t dir_lba, uint32_t dir_size,
                         iso9660_dirent_t out[ISO9660_MAX_FILES]);

/*
 * iso9660_find_file — locate a file in the root directory by name.
 * Name comparison is case-insensitive; version suffix (";1") is ignored.
 * Returns 1 and fills *out on success, 0 if not found, -1 on I/O error.
 */
int iso9660_find_file(uint8_t drive_index, const char *name,
                      iso9660_dirent_t *out);

/*
 * iso9660_read_file — read up to max_bytes of the file described by *ent
 * into buf.  Returns actual bytes read (may be less than ent->size if
 * max_bytes is smaller), or -1 on I/O error.
 */
int32_t iso9660_read_file(uint8_t drive_index, const iso9660_dirent_t *ent,
                          uint8_t *buf, uint32_t max_bytes);

#endif /* ISO9660_H */
