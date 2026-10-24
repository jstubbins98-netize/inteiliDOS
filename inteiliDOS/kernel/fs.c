/*
 * inteilidOS -- kernel/fs.c
 * Flat-file persistent storage over the ATA block driver.
 *
 * See kernel/fs.h for the on-disk layout description.
 */

#include "fs.h"
#include "ata.h"
#include "memory.h"
#include "disk_layout.h"
#include <stdint.h>

/* ── Constants ─────────────────────────────────────────────────────────── */

#define FS_MAGIC      NATIVE_FS_MAGIC
#define LEGACY_DIR_LBA 2048u
#define LEGACY_DATA_START 2049u

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
static uint8_t layout_buf[512] __attribute__((aligned(4)));
static uint32_t fs_dir_lba, fs_data_start, fs_limit;
static ata_drive_t fs_drives[ATA_MAX_DRIVES];
static int fs_drives_ready = 0;
static uint8_t fs_volumes[ATA_MAX_DRIVES];
static uint8_t fs_volume_count;

/* Resolve on every public operation: setup may change the disk in this boot.
 * No partition guessing, formatting, or migration is authorized here. */
static int fs_resolve_layout(uint8_t drv) {
    uint32_t total = fs_drives[drv].total_sectors;
    if (ata_read_sector(drv, 0, layout_buf)) return FS_ERR_IO;
    int signed_mbr = layout_buf[510] == 0x55 && layout_buf[511] == 0xaa;
    int slot = native_partition_find(layout_buf, total);
    if (slot == -2) return FS_ERR_LAYOUT;
    if (slot >= 0) {
        if (!signed_mbr) return FS_ERR_LAYOUT;
        native_part_t p = ((native_part_t *)(layout_buf + 446))[slot];
        if (ata_read_sector(drv, p.start, layout_buf)) return FS_ERR_IO;
        uint32_t config = native_config_offset(layout_buf);
        if (p.sectors <= config + NATIVE_CONFIG_SECTORS + 1u) return FS_ERR_LAYOUT;
        if (ata_read_sector(drv, p.start + config, layout_buf))
            return FS_ERR_IO;
        if (!native_layout_valid((native_layout_t *)layout_buf, p.start, p.sectors))
            return FS_ERR_LAYOUT;
        native_layout_t *layout = (native_layout_t *)layout_buf;
        if (layout->kernel_sectors + 1u != config) return FS_ERR_LAYOUT;
        fs_dir_lba = p.start + layout->dir_offset;
        fs_data_start = fs_dir_lba + 1u;
        fs_limit = p.start + p.sectors;
        return FS_OK;
    }
    /* Flat legacy filesystem is allowed only on an unpartitioned disk.
     * An unsigned sector 0 must be completely blank (not an unknown VBR). */
    for (int i = 0; i < 4; ++i)
        if (((native_part_t *)(layout_buf + 446))[i].type) return FS_ERR_LAYOUT;
    if (!signed_mbr)
        for (int i = 0; i < 512; ++i)
            if (layout_buf[i]) return FS_ERR_LAYOUT;
    if (total <= LEGACY_DATA_START) return FS_ERR_LAYOUT;
    fs_dir_lba = LEGACY_DIR_LBA;
    fs_data_start = LEGACY_DATA_START;
    fs_limit = total;
    return FS_OK;
}

/* ── ATA drive detection (lazy, once per boot) ──────────────────────────── */

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
static int fs_validate_dir(void);

static int fs_read_dir(uint8_t drv) {
    int rc = fs_resolve_layout(drv);
    if (rc) return rc;
    if (ata_read_sector(drv, fs_dir_lba, dir_buf) != 0)
        return FS_ERR_IO;

    uint32_t magic;
    kmemcpy(&magic, dir_buf, 4);
    if (magic != FS_MAGIC)
        return FS_ERR_NODIR;   /* let the caller decide; do NOT auto-wipe */

    return fs_validate_dir();
}

/*
 * Write dir_buf back to the directory sector on the drive.
 * Always ensures the magic word is present before writing.
 * Returns FS_OK or FS_ERR_IO.
 */
static int fs_write_dir(uint8_t drv) {
    uint32_t m = FS_MAGIC;
    kmemcpy(dir_buf, &m, 4);
    if (ata_write_sector(drv, fs_dir_lba, dir_buf) != 0)
        return FS_ERR_IO;
    return FS_OK;
}

/*
 * Scan all valid entries and return the lowest LBA above all current data.
 * Returns fs_data_start if the directory is empty.
 */
static uint32_t fs_next_free_lba(void) {
    uint32_t high = fs_data_start;
    for (int i = 0; i < FS_MAX_FILES; i++) {
        fs_dirent_t *e = fs_entry(i);
        if (e->start_lba == 0) continue;
        uint32_t sectors = e->byte_count / 512u + !!(e->byte_count % 512u);
        if (!sectors) sectors = 1;
        uint32_t end = e->start_lba + sectors;
        if (end > high) high = end;
    }
    return high;
}

/* ── Public API ─────────────────────────────────────────────────────────── */

int fs_volume_drive(uint8_t volume, uint8_t *drive) {
    if (!drive || volume >= ATA_MAX_DRIVES) return FS_ERR_NODRV;
    fs_ensure_drives();
    if (!fs_volume_count) {
        uint8_t rank[ATA_MAX_DRIVES];
        int error = FS_ERR_NODRV;
        for (uint8_t d = 0; d < ATA_MAX_DRIVES; ++d) {
            if (!fs_drives[d].present) continue;
            int rc = fs_read_dir(d);
            if (rc) {
                /* Preserve a useful reason when no safe native disk exists. */
                if (error == FS_ERR_NODRV || rc == FS_ERR_LAYOUT ||
                    (rc == FS_ERR_IO && error != FS_ERR_LAYOUT)) error = rc;
                continue;
            }
            /* fs_read_dir's scratch buffers no longer contain the MBR. */
            if (ata_read_sector(d, 0, layout_buf)) {
                if (error != FS_ERR_LAYOUT) error = FS_ERR_IO;
                continue;
            }
            int slot = native_partition_find(layout_buf, fs_drives[d].total_sectors);
            uint8_t priority = 2;
            if (slot >= 0)
                priority = ((native_part_t *)(layout_buf + 446))[slot].status == 0x80 ? 0 : 1;
            uint8_t pos = fs_volume_count;
            while (pos && rank[pos - 1] > priority) {
                rank[pos] = rank[pos - 1];
                fs_volumes[pos] = fs_volumes[pos - 1];
                --pos;
            }
            rank[pos] = priority;
            fs_volumes[pos] = d;
            ++fs_volume_count;
        }
        if (!fs_volume_count) return error;
    }
    if (volume >= fs_volume_count) return FS_ERR_NODRV;
    uint8_t d = fs_volumes[volume];
    int rc = fs_read_dir(d);
    if (rc) return rc; /* pinned volume: never fall through to another disk */
    *drive = d;
    return FS_OK;
}

int fs_volume_write(uint8_t volume, const char *name,
                    const uint8_t *data, uint32_t len) {
    uint8_t drive;
    int rc = fs_volume_drive(volume, &drive);
    return rc ? rc : fs_write(drive, name, data, len);
}

int fs_volume_read(uint8_t volume, const char *name,
                   uint8_t *buf, uint32_t buf_size, uint32_t *out_len) {
    uint8_t drive;
    if (out_len) *out_len = 0;
    int rc = fs_volume_drive(volume, &drive);
    return rc ? rc : fs_read(drive, name, buf, buf_size, out_len);
}

int fs_write(uint8_t drive_idx, const char *name,
             const uint8_t *data, uint32_t len) {
    /* ── Guard inputs ─────────────────────────────────────────────────── */
    if (!name || name[0] == '\0' || kstrlen(name) > FS_NAME_MAX ||
        (!data && len)) return FS_ERR_IO;
    if (len > FS_MAX_LEN)         return FS_ERR_BIG;

    /* ── Ensure ATA detection has happened ───────────────────────────── */
    fs_ensure_drives();

    if (drive_idx >= ATA_MAX_DRIVES)          return FS_ERR_NODRV;
    if (!fs_drives[drive_idx].present)         return FS_ERR_NODRV;

    /* Never auto-format a blank, foreign, or older installed disk. */
    int rc = fs_read_dir(drive_idx);
    if (rc != FS_OK) return rc;

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
    /* Large disk images are not editable as ordinary text/documents. */
    if (ent->start_lba && ent->byte_count > FS_MAX_LEN) return FS_ERR_BIG;
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

    if (write_lba >= fs_limit || sectors_needed > fs_limit - write_lba)
        return FS_ERR_FULL;

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

/* Validate every extent before allocating a private virtual disk. A magic
 * number alone is not sufficient authorization to write a physical disk. */
static int fs_validate_dir(void) {
    uint32_t limit = fs_limit;
    for (int i = 0; i < FS_MAX_FILES; ++i) {
        fs_dirent_t *e = fs_entry(i);
        if (!e->start_lba) {
            if (e->byte_count) return FS_ERR_IO;
            continue;
        }
        uint32_t n = e->byte_count / 512 + !!(e->byte_count % 512);
        if (!n) n = 1;
        if (e->start_lba < fs_data_start || e->start_lba >= limit ||
            n > limit - e->start_lba || e->name[23] || !e->name[0])
            return FS_ERR_IO;
        for (int j = 0; j < i; ++j) {
            fs_dirent_t *p = fs_entry(j);
            if (!p->start_lba) continue;
            uint32_t pn = p->byte_count / 512 + !!(p->byte_count % 512);
            if (!pn) pn = 1;
            if (fs_name_eq(e->name, p->name) ||
                (e->start_lba < p->start_lba + pn &&
                p->start_lba < e->start_lba + n)) return FS_ERR_IO;
        }
    }
    return FS_OK;
}

static int fs_image_directory(uint8_t drive) {
    fs_ensure_drives();
    if (drive >= ATA_MAX_DRIVES || !fs_drives[drive].present)
        return FS_ERR_NODRV;
    return fs_read_dir(drive);
}

int fs_image_open(uint8_t drive, const char *name, fs_image_t *image) {
    if (!name || !image) return FS_ERR_IO;
    int rc = fs_image_directory(drive);
    if (rc) return rc;
    for (int i = 0; i < FS_MAX_FILES; ++i) {
        fs_dirent_t *e = fs_entry(i);
        if (e->start_lba && fs_name_eq(e->name, name)) {
            if (!e->byte_count || e->byte_count % 512) return FS_ERR_IO;
            image->drive = drive;
            image->start = e->start_lba;
            image->sectors = e->byte_count / 512;
            return FS_OK;
        }
    }
    return FS_ERR_NOTFOUND;
}

int fs_image_space(uint8_t drive, fs_image_space_t *space) {
    if (!space) return FS_ERR_IO;
    int rc = fs_image_directory(drive);
    if (rc) return rc;
    fs_image_space_t result = {0, 0};
    for (int i = 0; i < FS_MAX_FILES; ++i)
        if (!fs_entry(i)->start_lba) ++result.free_slots;
    uint32_t first = fs_next_free_lba();
    result.available_sectors = fs_limit - first; /* validated extents */
    *space = result;
    return FS_OK;
}

int fs_image_reserve(uint8_t drive, uint32_t sectors, fs_image_t *image) {
    if (!image || !sectors || sectors > 0x7fffff) return FS_ERR_BIG;
    fs_image_space_t space;
    int rc = fs_image_space(drive, &space);
    if (rc) return rc;
    if (!space.free_slots) return FS_ERR_FULL;
    if (sectors > space.available_sectors) return FS_ERR_NOSPC;
    image->drive = drive;
    image->start = fs_limit - space.available_sectors;
    image->sectors = sectors;
    return FS_OK;
}

static int fs_image_bounds(const fs_image_t *image, uint32_t sector,
                           uint32_t count) {
    if (!image || !count || sector >= image->sectors ||
        count > image->sectors - sector ||
        image->drive >= ATA_MAX_DRIVES ||
        image->start > 0xffffffffu - sector) return FS_ERR_IO;
    fs_ensure_drives();
    if (!fs_drives[image->drive].present) return FS_ERR_NODRV;
    int rc = fs_resolve_layout(image->drive);
    if (rc) return rc;
    if (image->start < fs_data_start || image->start >= fs_limit ||
        image->sectors > fs_limit - image->start) return FS_ERR_IO;
    return FS_OK;
}

int fs_image_zero(const fs_image_t *image, uint32_t sector, uint32_t count) {
    int rc = fs_image_bounds(image, sector, count);
    if (rc) return rc;
    return ata_zero_sectors(image->drive, image->start + sector, count)
           ? FS_ERR_IO : FS_OK;
}

int fs_image_sector(const fs_image_t *image, uint32_t sector,
                    uint8_t *data, int write) {
    if (!data) return FS_ERR_IO;
    int rc = fs_image_bounds(image, sector, 1);
    if (rc) return rc;
    return (write ? ata_write_sector(image->drive, image->start + sector, data)
                  : ata_read_sector(image->drive, image->start + sector, data))
           ? FS_ERR_IO : FS_OK;
}

int fs_image_publish(const char *name, const fs_image_t *image) {
    if (!image || !name || !*name || kstrlen(name) > FS_NAME_MAX)
        return FS_ERR_IO;
    int rc = fs_image_directory(image->drive);
    if (rc) return rc;
    if (image->start != fs_next_free_lba() || !image->sectors ||
        image->sectors > 0x7fffff ||
        image->start >= fs_limit ||
        image->sectors > fs_limit - image->start)
        return FS_ERR_IO;
    int slot = -1;
    for (int i = 0; i < FS_MAX_FILES; ++i) {
        if (fs_entry(i)->start_lba && fs_name_eq(fs_entry(i)->name, name))
            return FS_ERR_IO;
        if (!fs_entry(i)->start_lba && slot < 0) slot = i;
    }
    if (slot < 0) return FS_ERR_FULL;
    fs_dirent_t *e = fs_entry(slot);
    kmemset(e, 0, sizeof(*e));
    fs_copy_name(e->name, name);
    e->start_lba = image->start;
    e->byte_count = image->sectors * 512;
    return fs_write_dir(image->drive);
}
