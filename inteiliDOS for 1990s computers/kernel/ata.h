/*
 * inteilidOS -- kernel/ata.h
 * ATA storage driver: PATA/IDE (legacy port I/O) and SATA/AHCI (MMIO).
 *
 * Drive index layout (ATA_MAX_DRIVES = 8):
 *   0  Primary   master  — PATA  0x1F0, slave bit 0
 *   1  Primary   slave   — PATA  0x1F0, slave bit 1
 *   2  Secondary master  — PATA  0x170, slave bit 0
 *   3  Secondary slave   — PATA  0x170, slave bit 1
 *   4  First  AHCI port that has a drive attached
 *   5  Second AHCI port that has a drive attached
 *   6  Third  AHCI port that has a drive attached
 *   7  Fourth AHCI port that has a drive attached
 *
 * Both PATA and AHCI drives are accessible through the same
 * ata_read_sector / ata_write_sector API.  The driver dispatches
 * internally based on each drive's type field.
 */

#ifndef ATA_H
#define ATA_H

#include <stdint.h>

/* Maximum number of drives tracked (4 PATA + 4 AHCI) */
#define ATA_MAX_DRIVES  8

/* Presence flags */
#define ATA_NOT_PRESENT 0
#define ATA_PRESENT     1

/* Bus type stored in ata_drive_t.drive_type */
#define ATA_TYPE_PATA   0   /* legacy IDE port I/O (0x1F0 / 0x170)      */
#define ATA_TYPE_AHCI   1   /* AHCI MMIO via HBA                         */

/* ata_drive_t — describes one detected drive */
typedef struct {
    uint8_t  present;           /* ATA_PRESENT or ATA_NOT_PRESENT             */
    uint8_t  drive_type;        /* ATA_TYPE_PATA or ATA_TYPE_AHCI             */
    uint8_t  drive_index;       /* index in the global drive table (0-7)      */
    uint8_t  _pad;
    uint32_t total_sectors;     /* total addressable 512-byte sectors          */
    char     model[41];         /* null-terminated model string                */

    /* AHCI-specific — valid only when drive_type == ATA_TYPE_AHCI */
    uint32_t ahci_abar;         /* AHCI Base Address Register (physical MMIO)  */
    uint8_t  ahci_port;         /* HBA port number (0-31)                      */
} ata_drive_t;

/*
 * ata_detect  — probe PATA and AHCI buses.
 *   Fills out[0..ATA_MAX_DRIVES-1].
 *   Returns total number of drives found.
 */
int  ata_detect(ata_drive_t out[ATA_MAX_DRIVES]);

/*
 * ata_read_sector  — read one 512-byte sector.
 *   drive_index : 0-7 (as assigned by ata_detect)
 *   lba         : 28-bit LBA address
 *   buf         : must be at least 512 bytes
 *   Returns 0 on success, -1 on error.
 */
int  ata_read_sector(uint8_t drive_index, uint32_t lba, uint8_t *buf);

/*
 * ata_write_sector  — write one 512-byte sector.
 *   drive_index : 0-7
 *   lba         : 28-bit LBA address
 *   buf         : 512 bytes to write
 *   Returns 0 on success, -1 on error.
 */
int  ata_write_sector(uint8_t drive_index, uint32_t lba, const uint8_t *buf);

#endif /* ATA_H */
