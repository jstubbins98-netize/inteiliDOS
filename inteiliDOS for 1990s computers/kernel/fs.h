/*
 * inteilidOS -- kernel/fs.h
 * Flat-file persistent storage over the ATA block driver.
 *
 * Disk layout (512-byte sectors):
 *   Installed type-0x99 MBR partition: see disk_layout.h for reserved VBR,
 *   kernel and configuration regions; directory/data follow those regions.
 *   Legacy unpartitioned disks: existing directory at LBA 2048, data 2049+.
 *   Entry LBAs remain physical. All extents must fit inside the file region.
 *   Unknown/blank disks and older installs are never auto-formatted/migrated.
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
 * Physical APIs retain ATA connector indexes (0..7), not drive letters.
 * Native application C:/D: use the logical-volume APIs below.
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
#define FS_ERR_LAYOUT (-7)  /* unsafe/unsupported layout; backup required */
#define FS_ERR_NOSPC  (-8)  /* insufficient contiguous image data space   */

/* Limits */
#define FS_MAX_FILES  15            /* directory entries per drive          */
#define FS_NAME_MAX   23            /* max characters in a filename         */
#define FS_MAX_LEN    (512u * 1024u)/* 512 KB max file size                 */

/*
 * Logical native volumes: 0 = C:, 1 = D:, etc.
 * Only fully validated directories qualify. Prefer active installed native
 * partitions, then other installed native partitions, then legacy flat disks;
 * ties use ascending physical ATA index. Selection is pinned for this boot
 * after the first successful discovery, and revalidated before use. Failure
 * never redirects a write to another disk. Discovery is strictly read-only.
 */
int fs_volume_drive(uint8_t volume, uint8_t *drive);
int fs_volume_write(uint8_t volume, const char *name,
                    const uint8_t *data, uint32_t len);
int fs_volume_read(uint8_t volume, const char *name,
                   uint8_t *buf, uint32_t buf_size, uint32_t *out_len);

/* Return codes (continued) */
/* FS_ERR_NOTFOUND (-5) already defined above */

/*
 * fs_write — persist raw bytes under name on the given ATA drive.
 *
 *   drive_idx : physical ATA index, 0 = primary master, 1 = primary slave
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
 *   drive_idx : physical ATA index, 0 = primary master, 1 = primary slave
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

/* Large private disk images. Never auto-format a physical drive.
 * reserve leaves the allocation unpublished until initialization succeeds.
 * All I/O is sector-aligned and bounded to the allocation. */
typedef struct {
    uint8_t drive;
    uint32_t start, sectors;
} fs_image_t;
/* Read-only, validated allocation information. Available sectors are the
 * appendable tail inside this volume, not holes or physical disk capacity.
 * Reserved boot/kernel/configuration sectors and existing files are excluded.
 * Output is assigned only on FS_OK; a query is not an allocation guarantee. */
typedef struct {
    uint32_t available_sectors;
    uint32_t free_slots;
} fs_image_space_t;
int fs_image_space(uint8_t drive, fs_image_space_t *space);
int fs_image_open(uint8_t drive, const char *name, fs_image_t *image);
int fs_image_reserve(uint8_t drive, uint32_t sectors, fs_image_t *image);
int fs_image_publish(const char *name, const fs_image_t *image);
int fs_image_sector(const fs_image_t *image, uint32_t sector,
                    uint8_t *data, int write);
int fs_image_zero(const fs_image_t *image, uint32_t sector, uint32_t count);

#endif /* FS_H */
