/*
 * Installed native partition layouts. All offsets are relative to
 * the MBR type-0x99 partition, not to the physical disk. Existing installations
 * without this descriptor must NOT be initialized by filesystem callers.
 */
#ifndef DISK_LAYOUT_H
#define DISK_LAYOUT_H
#include <stdint.h>

#define NATIVE_PART_TYPE       0x99u
#define NATIVE_PART_START      2048u
#define NATIVE_LEGACY_KERNEL_SECTORS 1024u
#define NATIVE_KERNEL_SECTORS  8192u /* 4 MiB; matches the 1..5 MiB RAM window */
#define NATIVE_CONFIG_OFFSET   (1u + NATIVE_KERNEL_SECTORS)
#define NATIVE_CONFIG_SECTORS  8u
#define NATIVE_DIR_OFFSET      (NATIVE_CONFIG_OFFSET + NATIVE_CONFIG_SECTORS)
#define NATIVE_DATA_OFFSET     (NATIVE_DIR_OFFSET + 1u)
#define NATIVE_LAYOUT_MAGIC    0x314c4449u /* IDL1 */
#define NATIVE_FS_MAGIC        0x46530001u

typedef struct {
    uint8_t status, chs_first[3], type, chs_last[3];
    uint32_t start, sectors;
} __attribute__((packed)) native_part_t;

typedef struct {
    uint32_t magic, version, part_start, part_sectors;
    uint32_t kernel_sectors, config_sectors, dir_offset, checksum;
} native_layout_t;

static inline uint32_t native_layout_checksum(const native_layout_t *l) {
    return l->magic ^ l->version ^ l->part_start ^ l->part_sectors ^
           l->kernel_sectors ^ l->config_sectors ^ l->dir_offset ^ 0x9e3779b9u;
}

static inline void native_layout_init(native_layout_t *l, uint32_t start,
                                      uint32_t sectors) {
    l->magic = NATIVE_LAYOUT_MAGIC;
    l->version = 2;
    l->part_start = start;
    l->part_sectors = sectors;
    l->kernel_sectors = NATIVE_KERNEL_SECTORS;
    l->config_sectors = NATIVE_CONFIG_SECTORS;
    l->dir_offset = NATIVE_DIR_OFFSET;
    l->checksum = native_layout_checksum(l);
}

static inline int native_layout_valid(const native_layout_t *l,
                                      uint32_t start, uint32_t sectors) {
    int supported = (l->version == 1 &&
                     l->kernel_sectors == NATIVE_LEGACY_KERNEL_SECTORS) ||
                    (l->version == 2 &&
                     l->kernel_sectors == NATIVE_KERNEL_SECTORS);
    return l->magic == NATIVE_LAYOUT_MAGIC && supported &&
           l->part_start == start && l->part_sectors == sectors &&
           l->config_sectors == NATIVE_CONFIG_SECTORS &&
           l->dir_offset == 1u + l->kernel_sectors + l->config_sectors &&
           l->checksum == native_layout_checksum(l) &&
           sectors > l->dir_offset + 1u;
}

/* Select by the versioned VBR marker, never by probing old user-data sectors
 * for a plausible descriptor. Unknown layouts remain blocked. */
#define NATIVE_VBR_V2_MAGIC 0x324c4449u /* IDL2 at VBR offset 0x25 */
static inline uint32_t native_config_offset(const uint8_t *vbr) {
    uint32_t marker = (uint32_t)vbr[0x25] | ((uint32_t)vbr[0x26] << 8) |
                      ((uint32_t)vbr[0x27] << 16) | ((uint32_t)vbr[0x28] << 24);
    return 1u + (marker == NATIVE_VBR_V2_MAGIC ?
                 NATIVE_KERNEL_SECTORS : NATIVE_LEGACY_KERNEL_SECTORS);
}

/* Validate ALL MBR extents, including foreign partitions, before using space.
 * Returns native slot, -1 for none, -2 for malformed/ambiguous partitions.
 * The caller must separately check the MBR signature. */
static inline int native_partition_find(const uint8_t *mbr, uint32_t total) {
    const native_part_t *p = (const native_part_t *)(mbr + 446);
    int found = -1;
    for (int i = 0; i < 4; ++i) {
        if (!p[i].type) {
            if (p[i].start || p[i].sectors || p[i].status) return -2;
            continue;
        }
        if ((p[i].status != 0 && p[i].status != 0x80) ||
            !p[i].start || !p[i].sectors || p[i].start >= total ||
            p[i].sectors > total - p[i].start) return -2;
        for (int j = 0; j < i; ++j) {
            if (p[j].type && p[i].start < p[j].start + p[j].sectors &&
                p[j].start < p[i].start + p[i].sectors) return -2;
        }
        if (p[i].type == NATIVE_PART_TYPE) {
            if (found >= 0) return -2;
            found = i;
        }
    }
    return found;
}
#endif
