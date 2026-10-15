/*
 * inteiliDOS -- kernel/iso9660.c
 * Minimal ISO 9660 filesystem reader (CD-ROM)
 *
 * Reads the Primary Volume Descriptor (LBA 16) to locate the root
 * directory, then iterates directory records to list files and locate
 * them for reading.  All I/O goes through cdrom_read_sector().
 */

#include "iso9660.h"
#include "cdrom.h"
#include <stdint.h>
#include <stddef.h>

/* ── Helpers ──────────────────────────────────────────────────────────── */

static inline uint32_t iso_le32(const uint8_t *p) {
    return (uint32_t)p[0]
         | ((uint32_t)p[1] <<  8)
         | ((uint32_t)p[2] << 16)
         | ((uint32_t)p[3] << 24);
}

static int iso_toupper(int c) {
    return (c >= 'a' && c <= 'z') ? c - 32 : c;
}

static int iso_streq_ci(const char *a, const char *b) {
    while (*a && *b) {
        if (iso_toupper((unsigned char)*a) != iso_toupper((unsigned char)*b))
            return 0;
        a++; b++;
    }
    return (*a == '\0' && *b == '\0');
}

/*
 * Copy a raw ISO 9660 file identifier into a C string.
 * Strips the ";N" version suffix and any trailing spaces or dots.
 * Converts to upper-case (ISO 9660 Level 1 names are already upper-case,
 * but Joliet discs may have mixed-case Rock Ridge extensions).
 */
static void iso_copy_name(char *dst, const uint8_t *src, int len,
                           int dst_max) {
    int n = (len < dst_max - 1) ? len : dst_max - 1;
    int i;
    for (i = 0; i < n; i++) {
        if (src[i] == ';') break;          /* strip version number */
        dst[i] = (char)src[i];
    }
    /* strip trailing spaces and dots */
    while (i > 0 && (dst[i-1] == ' ' || dst[i-1] == '.')) i--;
    dst[i] = '\0';
}

/* ── Static sector buffer (avoids large stack allocation) ─────────────── */
static uint8_t iso_sec[CDROM_SECTOR_SIZE];

/* ── API ──────────────────────────────────────────────────────────────── */

/*
 * iso_read_dir_sectors — shared inner loop used by both iso9660_read_dir
 * and iso9660_read_dir_at.  Reads directory records from a known
 * (dir_lba, dir_size) pair, skipping "." and ".." special entries.
 */
static int iso_read_dir_sectors(uint8_t drive_index,
                                 uint32_t dir_lba, uint32_t dir_sz,
                                 iso9660_dirent_t out[ISO9660_MAX_FILES]) {
    uint32_t dir_sects = (dir_sz + CDROM_SECTOR_SIZE - 1) / CDROM_SECTOR_SIZE;
    int count = 0;

    for (uint32_t s = 0; s < dir_sects && count < ISO9660_MAX_FILES; s++) {
        if (cdrom_read_sector(drive_index, dir_lba + s, iso_sec) != 0)
            break;

        uint32_t off = 0;
        while (off + 33 < CDROM_SECTOR_SIZE && count < ISO9660_MAX_FILES) {
            uint8_t rec_len  = iso_sec[off];
            if (rec_len == 0)
                break;  /* no more records in this sector */
            if (off + rec_len > CDROM_SECTOR_SIZE)
                break;  /* record overruns sector — malformed disc */

            uint8_t name_len = iso_sec[off + 32];
            uint8_t flags    = iso_sec[off + 25];

            /*
             * Skip the "." (0x00) and ".." (0x01) special entries;
             * they have name_len == 1 and a single non-printable byte.
             */
            if (!(name_len == 1 &&
                  (iso_sec[off + 33] == 0x00 || iso_sec[off + 33] == 0x01))) {
                iso_copy_name(out[count].name,
                              iso_sec + off + 33,
                              (int)name_len,
                              ISO9660_NAME_MAX);
                out[count].lba    = iso_le32(iso_sec + off + 2);
                out[count].size   = iso_le32(iso_sec + off + 10);
                out[count].is_dir = (flags & 0x02) ? 1u : 0u;
                count++;
            }

            off += rec_len;
        }
    }

    return count;
}

int iso9660_read_dir(uint8_t drive_index,
                     iso9660_dirent_t out[ISO9660_MAX_FILES]) {
    /* ── Read Primary Volume Descriptor at LBA 16 ── */
    if (cdrom_read_sector(drive_index, 16, iso_sec) != 0)
        return -1;

    /* Verify PVD: type byte = 0x01, identifier = "CD001", version = 0x01 */
    if (iso_sec[0] != 0x01 ||
        iso_sec[1] != 'C'  || iso_sec[2] != 'D' ||
        iso_sec[3] != '0'  || iso_sec[4] != '0' || iso_sec[5] != '1')
        return -1;

    /*
     * Root directory record is embedded in the PVD at byte offset 156.
     *   +2  : location of extent (start LBA) [LE uint32]
     *   +10 : data length (byte size)        [LE uint32]
     */
    const uint8_t *rdr     = iso_sec + 156;
    uint32_t       dir_lba = iso_le32(rdr + 2);
    uint32_t       dir_sz  = iso_le32(rdr + 10);

    return iso_read_dir_sectors(drive_index, dir_lba, dir_sz, out);
}

int iso9660_read_dir_at(uint8_t drive_index,
                         uint32_t dir_lba, uint32_t dir_size,
                         iso9660_dirent_t out[ISO9660_MAX_FILES]) {
    if (dir_lba == 0 || dir_size == 0) return -1;
    return iso_read_dir_sectors(drive_index, dir_lba, dir_size, out);
}

int iso9660_find_file(uint8_t drive_index, const char *name,
                      iso9660_dirent_t *out) {
    iso9660_dirent_t ents[ISO9660_MAX_FILES];
    int n = iso9660_read_dir(drive_index, ents);
    if (n < 0) return -1;
    for (int i = 0; i < n; i++) {
        if (iso_streq_ci(ents[i].name, name)) {
            *out = ents[i];
            return 1;
        }
    }
    return 0;
}

int32_t iso9660_read_file(uint8_t drive_index, const iso9660_dirent_t *ent,
                          uint8_t *buf, uint32_t max_bytes) {
    if (!ent || !buf || max_bytes == 0) return -1;

    uint32_t to_read = (ent->size < max_bytes) ? ent->size : max_bytes;
    uint32_t done    = 0;
    uint32_t sects   = (to_read + CDROM_SECTOR_SIZE - 1) / CDROM_SECTOR_SIZE;

    for (uint32_t s = 0; s < sects; s++) {
        if (cdrom_read_sector(drive_index, ent->lba + s, iso_sec) != 0)
            return (int32_t)done;  /* partial read */

        uint32_t chunk = CDROM_SECTOR_SIZE;
        if (done + chunk > to_read)
            chunk = to_read - done;

        for (uint32_t b = 0; b < chunk; b++)
            buf[done + b] = iso_sec[b];

        done += chunk;
    }

    return (int32_t)done;
}
