/*
 * Installed native partition layout, version 1. All offsets are relative to
 * the MBR type-0x99 partition, not to the physical disk. Existing installations
 * without this descriptor must NOT be initialized by filesystem callers.
 */
#ifndef DISK_LAYOUT_H
#define DISK_LAYOUT_H
#include <stdint.h>

#define NATIVE_PART_TYPE       0x99u
#define NATIVE_PART_START      2048u
#define NATIVE_KERNEL_SECTORS  1024u
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
    l->version = 1;
    l->part_start = start;
    l->part_sectors = sectors;
    l->kernel_sectors = NATIVE_KERNEL_SECTORS;
    l->config_sectors = NATIVE_CONFIG_SECTORS;
    l->dir_offset = NATIVE_DIR_OFFSET;
    l->checksum = native_layout_checksum(l);
}

static inline int native_layout_valid(const native_layout_t *l,
                                      uint32_t start, uint32_t sectors) {
    return l->magic == NATIVE_LAYOUT_MAGIC && l->version == 1 &&
           l->part_start == start && l->part_sectors == sectors &&
           l->kernel_sectors == NATIVE_KERNEL_SECTORS &&
           l->config_sectors == NATIVE_CONFIG_SECTORS &&
           l->dir_offset == NATIVE_DIR_OFFSET &&
           l->checksum == native_layout_checksum(l) &&
           sectors > NATIVE_DATA_OFFSET;
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
