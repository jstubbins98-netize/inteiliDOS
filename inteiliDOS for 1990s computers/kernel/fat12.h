/*
 * inteiliDOS -- kernel/fat12.h
 * FAT12 filesystem reader for floppy disks
 *
 * Reads the root directory and file data from a FAT12-formatted floppy
 * using fdc_read_sector() for all low-level I/O.
 * Supports 1.44 MB 3.5" disks.  Read-only; no write support.
 */

#ifndef FAT12_H
#define FAT12_H

#include <stdint.h>

#define FAT12_MAX_FILES 64
#define FAT12_NAME_MAX  13   /* "XXXXXXXX.XXX\0" */

#define FAT12_ATTR_READONLY  0x01
#define FAT12_ATTR_HIDDEN    0x02
#define FAT12_ATTR_SYSTEM    0x04
#define FAT12_ATTR_VOLUME    0x08
#define FAT12_ATTR_DIRECTORY 0x10
#define FAT12_ATTR_ARCHIVE   0x20

typedef struct {
    char     name[FAT12_NAME_MAX]; /* "FILENAME.EXT\0" (8.3 format)   */
    uint32_t size;                 /* file size in bytes               */
    uint16_t first_cluster;        /* starting cluster (2-based index) */
    uint8_t  attr;                 /* FAT attribute byte               */
} fat12_dirent_t;

/*
 * fat12_read_dir — list root directory entries of drive (0=A:, 1=B:).
 * Skips volume labels, deleted entries, and LFN stubs.
 * Returns number of entries found, or -1 on I/O error.
 */
int fat12_read_dir(uint8_t drive, fat12_dirent_t out[FAT12_MAX_FILES]);

/*
 * fat12_read_file — read up to max_bytes of the file described by *ent
 * into buf, following the FAT12 cluster chain.
 * Returns actual bytes read, or -1 on I/O error.
 */
int32_t fat12_read_file(uint8_t drive, const fat12_dirent_t *ent,
                        uint8_t *buf, uint32_t max_bytes);

/*
 * fat12_read_subdir — list entries inside a subdirectory cluster chain.
 *
 * Use this to navigate into subdirectories: pass the first_cluster from
 * the parent directory's fat12_dirent_t for the directory entry.
 *   drive         : 0 = A:, 1 = B:
 *   first_cluster : cluster of the subdirectory (must be >= 2)
 * Skips "." and ".." entries automatically.
 * Returns entry count (>= 0), or -1 on I/O error / bad cluster.
 */
int fat12_read_subdir(uint8_t drive, uint16_t first_cluster,
                      fat12_dirent_t out[FAT12_MAX_FILES]);

#endif /* FAT12_H */
