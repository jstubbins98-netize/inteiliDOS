/*
 * inteilidOS -- kernel/fs.c
 * Flat-file persistent storage over the ATA block driver.
 *
 * See kernel/fs.h for the on-disk layout description.
 */

#include "fs.h"
#include "ata.h"
#include "memory.h"
#include <stdint.h>

/* ── Constants ─────────────────────────────────────────────────────────── */

#define FS_MAGIC      0x46530001u   /* 'F','S',0x00,0x01                  */
#define FS_DIR_LBA    2048u         /* LBA of the directory sector         */
#define FS_DATA_START 2049u         /* first LBA available for file data   */

/* ── On-disk directory entry (32 bytes, packed) ─────────────────────────── */
typedef struct {
    char     name[24];      /* null-terminated filename (max 23 chars)    */
    uint32_t start_lba;     /* first data sector (0 = empty slot)         */
    uint32_t byte_count;    /* file size in bytes                         */
} __attribute__((packed)) fs_dirent_t;

/* Compile-time size assertion — must be exactly 32 bytes */
typedef char _fs_dirent_size_check[(sizeof(fs_dirent_t) == 32) ? 1 : -1];

/* ── Module-level sector buffers (static — never on the stack) ──────────── */

/*
 * Sector-aligned 512-byte buffers.
 * dir_buf   — holds the raw directory sector read from / written to disk.
 * data_buf  — staging area for each 512-byte data sector written.
 */
static uint8_t dir_buf[512]  __attribute__((aligned(4)));
static uint8_t data_buf[512] __attribute__((aligned(4)));

/* ── ATA drive detection (lazy, once per boot) ──────────────────────────── */

static ata_drive_t  fs_drives[ATA_MAX_DRIVES];
static int          fs_drives_ready = 0;

static void fs_ensure_drives(void) {
    if (!fs_drives_ready) {
        ata_detect(fs_drives);
        fs_drives_ready = 1;
    }
}

/* ── Name helpers ───────────────────────────────────────────────────────── */

/* Copy src into dst[24], null-terminating and truncating to FS_NAME_MAX. */
static void fs_copy_name(char dst[24], const char *src) {
    int i = 0;
    while (src[i] && i < FS_NAME_MAX) {
        dst[i] = src[i];
        i++;
    }
    dst[i] = '\0';
}

/* Case-sensitive name comparison for directory lookup. */
static int fs_name_eq(const char *a, const char *b) {
    int i = 0;
    while (a[i] && b[i]) {
        if (a[i] != b[i]) return 0;
        i++;
    }
    return (a[i] == '\0' && b[i] == '\0');
}

/* ── Directory helpers ──────────────────────────────────────────────────── */

/*
 * Return a pointer to directory entry i inside dir_buf.
 * Entries start at byte offset 4 (after the magic uint32_t).
 */
static fs_dirent_t *fs_entry(int i) {
    return (fs_dirent_t *)(dir_buf + 4 + (uint32_t)i * 32u);
}

/*
 * Read the directory sector from the drive and validate the magic word.
 * Returns FS_OK, FS_ERR_IO, or FS_ERR_NODIR (drive present but not
 * formatted for inteiliDOS — caller decides whether to format or abort).
 */
static int fs_read_dir(uint8_t drv) {
    if (ata_read_sector(drv, FS_DIR_LBA, dir_buf) != 0)
        return FS_ERR_IO;

    uint32_t magic;
    kmemcpy(&magic, dir_buf, 4);
    if (magic != FS_MAGIC)
        return FS_ERR_NODIR;   /* let the caller decide; do NOT auto-wipe */

    return FS_OK;
}

/* Initialise dir_buf as a fresh empty inteiliDOS directory (in memory only). */
static void fs_init_dir(void) {
    kmemset(dir_buf, 0, 512);
    uint32_t m = FS_MAGIC;
    kmemcpy(dir_buf, &m, 4);
}

/*
 * Write dir_buf back to the directory sector on the drive.
 * Always ensures the magic word is present before writing.
 * Returns FS_OK or FS_ERR_IO.
 */
static int fs_write_dir(uint8_t drv) {
    uint32_t m = FS_MAGIC;
    kmemcpy(dir_buf, &m, 4);
    if (ata_write_sector(drv, FS_DIR_LBA, dir_buf) != 0)
        return FS_ERR_IO;
    return FS_OK;
}

/*
 * Scan all valid entries and return the lowest LBA above all current data.
 * Returns FS_DATA_START if the directory is empty.
 */
static uint32_t fs_next_free_lba(void) {
    uint32_t high = FS_DATA_START;
    for (int i = 0; i < FS_MAX_FILES; i++) {
        fs_dirent_t *e = fs_entry(i);
        if (e->start_lba == 0 || e->byte_count == 0) continue;
        uint32_t end = e->start_lba + (e->byte_count + 511u) / 512u;
        if (end > high) high = end;
    }
    return high;
}

/* ── Public API ─────────────────────────────────────────────────────────── */

int fs_write(uint8_t drive_idx, const char *name,
             const uint8_t *data, uint32_t len) {
    /* ── Guard inputs ─────────────────────────────────────────────────── */
    if (!name || name[0] == '\0') return FS_ERR_IO;
    if (len > FS_MAX_LEN)         return FS_ERR_BIG;

    /* ── Ensure ATA detection has happened ───────────────────────────── */
    fs_ensure_drives();

    if (drive_idx >= ATA_MAX_DRIVES)          return FS_ERR_NODRV;
    if (!fs_drives[drive_idx].present)         return FS_ERR_NODRV;

    /* ── Load directory (auto-format if drive is blank/unformatted) ─── */
    int rc = fs_read_dir(drive_idx);
    if (rc == FS_ERR_NODIR) {
        /* First write to this drive: initialise an empty directory in
         * memory only; fs_write_dir will commit it at the end.          */
        fs_init_dir();
    } else if (rc != FS_OK) {
        return rc;
    }

    /* ── Locate existing entry or find an empty slot ─────────────────── */
    int slot = -1;
    for (int i = 0; i < FS_MAX_FILES; i++) {
        fs_dirent_t *e = fs_entry(i);
        if (e->start_lba != 0 && fs_name_eq(e->name, name)) {
            slot = i;   /* overwrite existing */
            break;
        }
    }
    if (slot < 0) {
        /* No existing entry — find a free slot. */
        for (int i = 0; i < FS_MAX_FILES; i++) {
            fs_dirent_t *e = fs_entry(i);
            if (e->start_lba == 0) { slot = i; break; }
        }
    }
    if (slot < 0) return FS_ERR_FULL;

    /* ── Decide where to write file data ─────────────────────────────── */
    fs_dirent_t *ent = fs_entry(slot);
    uint32_t sectors_needed = (len + 511u) / 512u;
    if (sectors_needed == 0) sectors_needed = 1;   /* always write ≥ 1 sector */

    uint32_t write_lba;
    if (ent->start_lba != 0) {
        /* Existing entry: reuse LBA if the slot is large enough */
        uint32_t old_sectors = (ent->byte_count + 511u) / 512u;
        if (old_sectors == 0) old_sectors = 1;
        if (sectors_needed <= old_sectors) {
            write_lba = ent->start_lba;   /* fits in the old allocation   */
        } else {
            write_lba = fs_next_free_lba();  /* append past existing data */
        }
    } else {
        write_lba = fs_next_free_lba();   /* new file: append at end      */
    }

    /* ── Write file sectors ──────────────────────────────────────────── */
    uint32_t offset = 0;
    for (uint32_t s = 0; s < sectors_needed; s++) {
        /* Fill data_buf with the next 512 bytes (pad remainder with 0s) */
        kmemset(data_buf, 0, 512);
        uint32_t chunk = 512u;
        if (offset + chunk > len) chunk = len - offset;
        if (offset < len)
            kmemcpy(data_buf, data + offset, chunk);

        if (ata_write_sector(drive_idx, write_lba + s, data_buf) != 0)
            return FS_ERR_IO;

        offset += chunk;
    }

    /* ── Update directory entry ──────────────────────────────────────── */
    fs_copy_name(ent->name, name);
    ent->start_lba  = write_lba;
    ent->byte_count = len;

    /* ── Flush directory ─────────────────────────────────────────────── */
    return fs_write_dir(drive_idx);
}

int fs_read(uint8_t drive_idx, const char *name,
            uint8_t *buf, uint32_t buf_size, uint32_t *out_len) {
    if (!name || name[0] == '\0' || !buf || buf_size == 0)
        return FS_ERR_IO;
    if (out_len) *out_len = 0;

    fs_ensure_drives();

    if (drive_idx >= ATA_MAX_DRIVES)    return FS_ERR_NODRV;
    if (!fs_drives[drive_idx].present)  return FS_ERR_NODRV;

    /* Load directory — unformatted drive means no files can exist */
    int rc = fs_read_dir(drive_idx);
    if (rc == FS_ERR_NODIR) return FS_ERR_NOTFOUND;
    if (rc != FS_OK)        return rc;

    /* Find the entry */
    fs_dirent_t *ent = (fs_dirent_t *)0;
    for (int i = 0; i < FS_MAX_FILES; i++) {
        fs_dirent_t *e = fs_entry(i);
        if (e->start_lba != 0 && fs_name_eq(e->name, name)) {
            ent = e;
            break;
        }
    }
    if (!ent) return FS_ERR_NOTFOUND;

    /* Clamp read length to what caller can hold */
    uint32_t file_len = ent->byte_count;
    uint32_t read_len = file_len < buf_size ? file_len : buf_size;
    uint32_t sectors  = (read_len + 511u) / 512u;
    if (sectors == 0) sectors = 1;   /* guard against zero-byte file */

    uint32_t offset = 0;
    for (uint32_t s = 0; s < sectors; s++) {
        if (ata_read_sector(drive_idx, ent->start_lba + s, data_buf) != 0)
            return FS_ERR_IO;
        uint32_t chunk = 512u;
        if (offset + chunk > read_len) chunk = (offset < read_len) ? read_len - offset : 0;
        if (chunk > 0) {
            kmemcpy(buf + offset, data_buf, chunk);
            offset += chunk;
        }
    }

    if (out_len) *out_len = read_len;
    return FS_OK;
}
