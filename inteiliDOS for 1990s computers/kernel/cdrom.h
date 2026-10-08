/*
 * inteiliDOS -- kernel/cdrom.h
 * ATAPI CD-ROM / DVD-ROM driver (PIO, no DMA)
 *
 * Works on top of the same IDE port pair as the ATA driver.  ATAPI devices
 * are identified by the signature bytes they leave in the LBA-mid / LBA-high
 * registers after IDENTIFY PACKET DEVICE: 0x14/0xEB (or 0xEB/0x14).
 *
 * Data is transferred in 2048-byte chunks (Mode 1 / Mode 2 Form 1 sectors).
 * Commands are issued as 12-byte SCSI Command Descriptor Blocks (CDBs)
 * tunnelled through the ATA PACKET command (0xA0).
 *
 * Drive indices match those used by ata.h:
 *   0 = Primary   Master   (base 0x1F0)
 *   1 = Primary   Slave    (base 0x1F0)
 *   2 = Secondary Master   (base 0x170)
 *   3 = Secondary Slave    (base 0x170)
 */

#ifndef CDROM_H
#define CDROM_H

#include <stdint.h>

/* Presence flags */
#define CDROM_NOT_PRESENT  0
#define CDROM_PRESENT      1

/* Maximum drives we track (matches ATA_MAX_DRIVES) */
#define CDROM_MAX_DRIVES   4

/* Standard CD-ROM logical block size (Mode 1 / Mode 2 Form 1) */
#define CDROM_SECTOR_SIZE  2048

/*
 * cdrom_drive_t — describes one detected ATAPI drive.
 *
 * last_lba   : index of the last readable logical block (0 if no disc or
 *               READ CAPACITY failed).  last_lba + 1 = total sectors.
 * block_size : bytes per logical block as reported by the drive.  Should
 *               always be 2048 for standard CDs; DVD-ROMs report 2048 too.
 */
typedef struct {
    uint8_t  present;        /* CDROM_PRESENT or CDROM_NOT_PRESENT */
    uint8_t  drive_index;    /* 0–3 (matches ata_drive_t index)    */
    uint32_t last_lba;       /* last readable LBA (from READ CAPACITY) */
    uint32_t block_size;     /* bytes per sector reported by drive  */
} cdrom_drive_t;

/*
 * cdrom_detect  — scan all four IDE positions for ATAPI devices.
 *
 * Fills out[0..CDROM_MAX_DRIVES-1].  Drives that are not ATAPI have
 * out[i].present = CDROM_NOT_PRESENT.
 * Returns the number of ATAPI drives found.
 *
 * Note: the ATA driver (ata_detect) deliberately skips ATAPI devices
 * by checking for non-zero LBA mid/high.  cdrom_detect checks for the
 * specific ATAPI signature values instead, so the two drivers do not
 * conflict.
 */
int cdrom_detect(cdrom_drive_t out[CDROM_MAX_DRIVES]);

/*
 * cdrom_read_sector  — read one 2048-byte CD-ROM sector.
 *
 * drive_index : 0–3
 * lba         : logical block address on the disc
 * buf         : caller-supplied buffer, must be at least CDROM_SECTOR_SIZE bytes
 * Returns 0 on success, -1 on error (drive absent, no disc, read error).
 */
int cdrom_read_sector(uint8_t drive_index, uint32_t lba, uint8_t *buf);

/*
 * cdrom_eject  — open the disc tray.
 *
 * drive_index : 0–3
 * Returns 0 on success, -1 if the command could not be sent or was rejected.
 * Some drives (especially laptop slim drives) do not support software eject.
 */
int cdrom_eject(uint8_t drive_index);

/*
 * cdrom_init  — detect drives and print a boot-sequence status line.
 *
 * Stores results in the module-internal drive table.  Must be called once
 * during kernel initialisation after the PIT timer is running (cdrom_detect
 * does not call timer_sleep but does use busy-wait loops; with sti active,
 * the wait loops are slightly more accurate).
 */
void cdrom_init(void);

/*
 * cdrom_count  — return the number of ATAPI drives found (after cdrom_init).
 */
int cdrom_count(void);

/*
 * cdrom_drives  — return a pointer to the internal cdrom_drive_t array.
 *
 * The array has CDROM_MAX_DRIVES entries; check present != CDROM_NOT_PRESENT
 * before using any entry.  Valid after cdrom_init() is called.
 */
const cdrom_drive_t *cdrom_drives(void);

/*
 * cdrom_rescan_media  — re-issue READ CAPACITY on one already-detected drive.
 *
 * Does NOT re-identify the drive (IDENTIFY PACKET DEVICE is not sent).
 * Only valid for a drive whose present == CDROM_PRESENT.  Updates the
 * internal table's last_lba and block_size in place.
 *
 * Use this to detect a disc inserted after boot without a full rescan.
 *   drive_index : 0–3
 * Returns the new last_lba (0 = no disc / tray empty), or 0 on error.
 */
uint32_t cdrom_rescan_media(uint8_t drive_index);

/*
 * cdrom_rescan_all  — call cdrom_rescan_media on every CDROM_PRESENT drive.
 *
 * Use after F3 (Rescan) or after inserting / swapping a disc at runtime.
 */
void cdrom_rescan_all(void);

#endif /* CDROM_H */
