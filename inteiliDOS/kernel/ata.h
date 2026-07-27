#ifndef ATA_H
#define ATA_H

/*
 * inteilidOS -- kernel/ata.h
 * Bare-metal ATA PIO driver (no BIOS, no DMA)
 *
 * Supports up to 4 drives:
 *   0 = Primary   Master   (base 0x1F0, drive bit = 0)
 *   1 = Primary   Slave    (base 0x1F0, drive bit = 1)
 *   2 = Secondary Master   (base 0x170, drive bit = 0)
 *   3 = Secondary Slave    (base 0x170, drive bit = 1)
 */

#include <stdint.h>

/* Maximum number of ATA drives we track */
#define ATA_MAX_DRIVES  4

/* Presence flag returned by ata_detect */
#define ATA_NOT_PRESENT 0
#define ATA_PRESENT     1

/* ata_drive_t — describes one detected drive */
typedef struct {
    uint8_t  present;           /* ATA_PRESENT or ATA_NOT_PRESENT      */
    uint8_t  drive_index;       /* 0-3 (matches index in ata_drives[]) */
    uint32_t total_sectors;     /* total addressable 512-byte sectors   */
    char     model[41];         /* null-terminated model string         */
} ata_drive_t;

/*
 * ata_detect  — probe all four drive positions.
 *   Fills out[0..3].  Returns the number of drives present.
 */
int  ata_detect(ata_drive_t out[ATA_MAX_DRIVES]);

/*
 * ata_read_sector  — read one 512-byte sector.
 *   drive_index : 0-3
 *   lba         : 28-bit LBA address
 *   buf         : must be at least 512 bytes
 *   returns 0 on success, -1 on error
 */
int  ata_read_sector(uint8_t drive_index, uint32_t lba, uint8_t *buf);

/*
 * ata_write_sector  — write one 512-byte sector.
 *   drive_index : 0-3
 *   lba         : 28-bit LBA address
 *   buf         : 512 bytes of data to write
 *   returns 0 on success, -1 on error
 */
int  ata_write_sector(uint8_t drive_index, uint32_t lba, const uint8_t *buf);

#endif /* ATA_H */
