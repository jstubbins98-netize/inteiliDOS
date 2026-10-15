/*
 * inteiliDOS -- kernel/fat12.c
 * FAT12 filesystem reader for 1.44 MB floppy disks
 *
 * Reads the BIOS Parameter Block from the boot sector to discover the
 * disk geometry, then traverses the FAT12 cluster chain to list and
 * read files.  All sector I/O is delegated to fdc_read_sector().
 */

#include "fat12.h"
#include "fdc.h"
#include <stdint.h>
#include <stddef.h>

/* ── BPB field offsets in the 512-byte boot sector ───────────────────── */
#define BPB_BYTES_PER_SEC   11u   /* uint16_t                            */
#define BPB_SECS_PER_CLUS   13u   /* uint8_t                             */
#define BPB_RSVD_SECS       14u   /* uint16_t  — sectors before FAT 1    */
#define BPB_NUM_FATS        16u   /* uint8_t                             */
#define BPB_ROOT_ENT_CNT    17u   /* uint16_t  — root directory entries  */
#define BPB_FAT_SZ_16       22u   /* uint16_t  — sectors per FAT         */
#define BPB_SECS_PER_TRK    24u   /* uint16_t                            */
#define BPB_NUM_HEADS       26u   /* uint16_t                            */

/* ── Helpers ──────────────────────────────────────────────────────────── */

static inline uint16_t le16(const uint8_t *p) {
    return (uint16_t)((uint16_t)p[0] | ((uint16_t)p[1] << 8));
}
static inline uint32_t le32(const uint8_t *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8)
         | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

/* Convert FAT 8.3 name (11 bytes, space-padded) to "NAME.EXT\0". */
static void fat_83_to_str(const uint8_t *raw, char *out) {
    int i, j = 0;
    /* Name part (8 bytes) */
    for (i = 0; i < 8 && raw[i] != ' '; i++)
        out[j++] = (char)raw[i];
    /* Extension (3 bytes) */
    if (raw[8] != ' ') {
        out[j++] = '.';
        for (i = 8; i < 11 && raw[i] != ' '; i++)
            out[j++] = (char)raw[i];
    }
    out[j] = '\0';
}

/* ── Static buffers ───────────────────────────────────────────────────── */
static uint8_t fat_sec[512];            /* one sector scratch buffer      */
static uint8_t fat_table[9 * 512];      /* FAT1 (≤9 sectors = 4608 bytes) */

/* ── Parsed BPB values (set by fat12_load_bpb) ───────────────────────── */
typedef struct {
    uint16_t bytes_per_sec;
    uint8_t  secs_per_clus;
    uint16_t rsvd_secs;
    uint8_t  num_fats;
    uint16_t root_ent_cnt;
    uint16_t fat_sz;
    /* Derived */
    uint32_t fat1_start;    /* LBA of FAT 1                              */
    uint32_t root_start;    /* LBA of root directory                     */
    uint32_t data_start;    /* LBA of cluster 2                          */
} fat12_bpb_t;

static int fat12_load_bpb(uint8_t drive, fat12_bpb_t *bpb) {
    if (fdc_read_sector(drive, 0, fat_sec) != 0)
        return -1;

    bpb->bytes_per_sec = le16(fat_sec + BPB_BYTES_PER_SEC);
    bpb->secs_per_clus = fat_sec[BPB_SECS_PER_CLUS];
    bpb->rsvd_secs     = le16(fat_sec + BPB_RSVD_SECS);
    bpb->num_fats      = fat_sec[BPB_NUM_FATS];
    bpb->root_ent_cnt  = le16(fat_sec + BPB_ROOT_ENT_CNT);
    bpb->fat_sz        = le16(fat_sec + BPB_FAT_SZ_16);

    if (bpb->bytes_per_sec != 512 || bpb->secs_per_clus == 0)
        return -1;

    bpb->fat1_start = bpb->rsvd_secs;
    bpb->root_start = bpb->fat1_start + (uint32_t)bpb->num_fats * bpb->fat_sz;
    uint32_t root_secs = ((uint32_t)bpb->root_ent_cnt * 32u + 511u) / 512u;
    bpb->data_start = bpb->root_start + root_secs;

    return 0;
}

/* Load FAT1 into fat_table[]. */
static int fat12_load_fat(uint8_t drive, const fat12_bpb_t *bpb) {
    uint16_t secs = bpb->fat_sz > 9u ? 9u : bpb->fat_sz;
    for (uint16_t s = 0; s < secs; s++) {
        if (fdc_read_sector(drive, bpb->fat1_start + s,
                            fat_table + (uint32_t)s * 512u) != 0)
            return -1;
    }
    return 0;
}

/* Follow the FAT12 chain: return the next cluster, or 0xFFF on EOF. */
static uint16_t fat12_next_cluster(uint16_t clus) {
    uint32_t off  = (uint32_t)clus * 3u / 2u;
    uint16_t word = (uint16_t)(fat_table[off] | ((uint16_t)fat_table[off + 1u] << 8));
    if (clus & 1u)
        return (uint16_t)(word >> 4);
    else
        return (uint16_t)(word & 0x0FFFu);
}

/* ── API ──────────────────────────────────────────────────────────────── */

int fat12_read_dir(uint8_t drive, fat12_dirent_t out[FAT12_MAX_FILES]) {
    fat12_bpb_t bpb;
    if (fat12_load_bpb(drive, &bpb) != 0) return -1;

    uint32_t root_secs = ((uint32_t)bpb.root_ent_cnt * 32u + 511u) / 512u;
    int count = 0;

    for (uint32_t s = 0; s < root_secs && count < FAT12_MAX_FILES; s++) {
        if (fdc_read_sector(drive, bpb.root_start + s, fat_sec) != 0)
            break;

        for (int e = 0; e < 512 / 32 && count < FAT12_MAX_FILES; e++) {
            const uint8_t *ent = fat_sec + e * 32;
            uint8_t first = ent[0];

            if (first == 0x00) goto done;   /* no more entries */
            if (first == 0xE5) continue;    /* deleted         */

            uint8_t attr = ent[11];

            /* Skip volume labels and LFN stubs */
            if ((attr & FAT12_ATTR_VOLUME)    && !(attr & FAT12_ATTR_DIRECTORY))
                continue;
            if ((attr & 0x0Fu) == 0x0Fu)      /* LFN entry */
                continue;

            fat_83_to_str(ent, out[count].name);
            out[count].size          = le32(ent + 28);
            out[count].first_cluster = le16(ent + 26);
            out[count].attr          = attr;
            count++;
        }
    }
done:
    return count;
}

int fat12_read_subdir(uint8_t drive, uint16_t first_cluster,
                      fat12_dirent_t out[FAT12_MAX_FILES]) {
    if (first_cluster < 2u) return -1;

    fat12_bpb_t bpb;
    if (fat12_load_bpb(drive, &bpb) != 0) return -1;
    if (fat12_load_fat(drive, &bpb)  != 0) return -1;

    int count = 0;
    uint16_t clus = first_cluster;

    while (clus >= 2u && clus < 0xFF8u && count < FAT12_MAX_FILES) {
        for (uint8_t s = 0; s < bpb.secs_per_clus && count < FAT12_MAX_FILES; s++) {
            uint32_t lba = bpb.data_start
                         + (uint32_t)(clus - 2u) * bpb.secs_per_clus
                         + s;
            if (fdc_read_sector(drive, lba, fat_sec) != 0)
                return count > 0 ? count : -1;

            for (int e = 0; e < 512 / 32 && count < FAT12_MAX_FILES; e++) {
                const uint8_t *ent = fat_sec + e * 32;
                uint8_t first = ent[0];

                if (first == 0x00) return count;  /* end of directory  */
                if (first == 0xE5) continue;       /* deleted entry     */
                if (first == (uint8_t)'.') continue; /* skip . and ..   */

                uint8_t attr = ent[11];
                /* volume label */
                if ((attr & FAT12_ATTR_VOLUME) && !(attr & FAT12_ATTR_DIRECTORY))
                    continue;
                /* LFN stub */
                if ((attr & 0x0Fu) == 0x0Fu)
                    continue;

                fat_83_to_str(ent, out[count].name);
                out[count].size          = le32(ent + 28);
                out[count].first_cluster = le16(ent + 26);
                out[count].attr          = attr;
                count++;
            }
        }
        clus = fat12_next_cluster(clus);
    }

    return count;
}

int32_t fat12_read_file(uint8_t drive, const fat12_dirent_t *ent,
                        uint8_t *buf, uint32_t max_bytes) {
    if (!ent || !buf || max_bytes == 0) return -1;
    if (ent->first_cluster < 2u) return 0;

    fat12_bpb_t bpb;
    if (fat12_load_bpb(drive, &bpb) != 0) return -1;
    if (fat12_load_fat(drive, &bpb)  != 0) return -1;

    uint32_t done    = 0;
    uint32_t to_read = ent->size < max_bytes ? ent->size : max_bytes;
    uint16_t clus    = ent->first_cluster;

    while (clus >= 2u && clus < 0xFF8u && done < to_read) {
        /* Each cluster = secs_per_clus × 512 bytes. */
        for (uint8_t s = 0; s < bpb.secs_per_clus && done < to_read; s++) {
            uint32_t lba = bpb.data_start
                         + (uint32_t)(clus - 2u) * bpb.secs_per_clus
                         + s;
            if (fdc_read_sector(drive, lba, fat_sec) != 0)
                return (int32_t)done;

            uint32_t chunk = 512u;
            if (done + chunk > to_read) chunk = to_read - done;
            for (uint32_t b = 0; b < chunk; b++)
                buf[done + b] = fat_sec[b];
            done += chunk;
        }
        clus = fat12_next_cluster(clus);
    }

    return (int32_t)done;
}
