/*
 * inteiliDOS -- kernel/fdc.h
 * Intel 8272A / 82077AA Floppy Disk Controller driver
 *
 * Drives the primary FDC (I/O base 0x3F0) in DMA mode.
 * Compatible with the PIIX4E integrated FDC on the HP Vectra VEi8.
 * Supports standard 3.5" 1.44 MB floppy disks (80 tracks, 2 heads,
 * 18 sectors/track, 512 bytes/sector, MFM encoding, 500 Kbps).
 */

#ifndef FDC_H
#define FDC_H

#include <stdint.h>

/* ── Error codes returned by fdc_read_sector ──────────────────────────────
 *
 * Callers should use these named constants rather than testing for -1 so
 * that the failure mode (timeout vs. hardware error vs. data integrity
 * error) can be reported accurately to the user.
 *
 * 0        success
 * -1       invalid parameter (bad drive, LBA out of range, NULL buffer)
 *          or seek / recalibrate failed
 * -2  FDC_ERR_TIMEOUT   FDC did not enter result phase within ~2 seconds;
 *                       the drive may be missing, spinning down, or the
 *                       controller is not responding.
 * -3  FDC_ERR_ABNORMAL  ST0 bits [7:6] = 10b — the FDC reported an
 *                       abnormal termination.  Common cause: no disk
 *                       inserted, or the sector address is out of range.
 * -4  FDC_ERR_CRC       ST1 bit 5 or ST2 bit 5 set — CRC mismatch in the
 *                       ID field or data field.  The media surface may be
 *                       damaged.
 *
 * fdc_read_sector() performs a soft-reset and one automatic retry on any
 * FDC_ERR_ABNORMAL or FDC_ERR_CRC error before returning to the caller.
 * FDC_ERR_TIMEOUT is not retried (the controller is unresponsive).
 */
#define FDC_ERR_TIMEOUT   (-2)
#define FDC_ERR_ABNORMAL  (-3)
#define FDC_ERR_CRC       (-4)

/*
 * fdc_init — reset the FDC, send SPECIFY timings, and recalibrate drive A:.
 * Must be called once during kernel init (after timer is running).
 * Returns 0 on success, -1 if the controller does not respond.
 */
int fdc_init(void);

/*
 * fdc_read_sector — read one 512-byte sector from floppy drive into buf.
 *   drive : 0 = A:, 1 = B:
 *   lba   : logical block address (0–2879 for a 1.44 MB disk)
 *   buf   : caller-supplied 512-byte output buffer
 *
 * Returns 0 on success.
 * Returns FDC_ERR_TIMEOUT, FDC_ERR_ABNORMAL, or FDC_ERR_CRC on distinct
 * hardware failure modes.  Returns -1 for parameter or seek errors.
 *
 * On FDC_ERR_ABNORMAL or FDC_ERR_CRC the driver performs a soft-reset and
 * one automatic retry before giving up.
 *
 * Call fdc_last_error_get() to retrieve the most recent error code after
 * an operation fails at a higher layer (e.g. FAT12 layer).
 */
int fdc_read_sector(uint8_t drive, uint32_t lba, uint8_t *buf);

/*
 * fdc_last_error_get — return the error code from the most recent
 * fdc_read_sector call that failed (one of FDC_ERR_* or -1).
 * Returns 0 if the last fdc_read_sector call succeeded.
 */
int fdc_last_error_get(void);

#endif /* FDC_H */
