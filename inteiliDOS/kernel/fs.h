/*
 * inteilidOS -- kernel/fs.h
 * Flat-file persistent storage over the ATA block driver.
 *
 * Disk layout (512-byte sectors):
 *   LBA 2048   Directory sector — magic header + up to FS_MAX_FILES entries
 *   LBA 2049+  File data, packed sequentially
 *
 * Directory sector (512 bytes):
 *   Bytes  0-3   : uint32_t magic  (FS_MAGIC = 0x46530001)
 *   Bytes  4-483 : FS_MAX_FILES × 32-byte entries
 *   Bytes 484-511: reserved (zero)
 *
 * Each 32-byte directory entry:
 *   Bytes  0-23 : char name[24]       — null-terminated filename
 *   Bytes 24-27 : uint32_t start_lba  — first data sector (0 = empty slot)
 *   Bytes 28-31 : uint32_t byte_count — file size in bytes
 *
 * ATA drive mapping: "C:\" → drive index 0,  "D:\" → drive index 1.
 * Drives that are not present silently return FS_ERR_NODRV.
 */

#ifndef FS_H
#define FS_H

#include <stdint.h>

/* Return codes */
#define FS_OK           0
#define FS_ERR_NODRV  (-1)   /* no ATA drive present at the given index     */
#define FS_ERR_FULL   (-2)   /* directory is full (FS_MAX_FILES reached)     */
#define FS_ERR_IO     (-3)   /* ATA read or write error                      */
#define FS_ERR_BIG    (-4)   /* file too large for this driver (> FS_MAX_LEN)*/
#define FS_ERR_NOTFOUND (-5) /* no file with the given name exists           */
#define FS_ERR_NODIR  (-6)   /* drive present but not formatted for inteiliDOS */

/* Limits */
#define FS_MAX_FILES  15            /* directory entries per drive          */
#define FS_NAME_MAX   23            /* max characters in a filename         */
#define FS_MAX_LEN    (512u * 1024u)/* 512 KB max file size                 */

/* Return codes (continued) */
/* FS_ERR_NOTFOUND (-5) already defined above */

/*
 * fs_write — persist raw bytes under name on the given ATA drive.
 *
 *   drive_idx : 0 = "C:\" (primary master), 1 = "D:\" (primary slave)
 *   name      : filename (≤ FS_NAME_MAX chars, no path prefix required)
 *   data      : bytes to write
 *   len       : number of bytes
 *
 * If a file with the same name already exists it is overwritten.
 * Returns FS_OK on success, or a negative FS_ERR_* code on failure.
 */
int fs_write(uint8_t drive_idx, const char *name,
             const uint8_t *data, uint32_t len);

/*
 * fs_read — load a previously saved file from the given ATA drive.
 *
 *   drive_idx : 0 = "C:\" (primary master), 1 = "D:\" (primary slave)
 *   name      : filename to look up (case-sensitive, ≤ FS_NAME_MAX chars)
 *   buf       : caller-supplied buffer to receive the file data
 *   buf_size  : size of buf in bytes; read is clamped to this limit
 *   out_len   : set to the number of bytes actually read on FS_OK
 *
 * Returns FS_OK on success, FS_ERR_NOTFOUND if the file does not exist,
 * or another negative FS_ERR_* code on I/O failure.
 */
int fs_read(uint8_t drive_idx, const char *name,
            uint8_t *buf, uint32_t buf_size, uint32_t *out_len);

#endif /* FS_H */
