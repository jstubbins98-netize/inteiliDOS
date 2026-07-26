/*
 * inteilidOS -- kernel/ata.c
 * Bare-metal ATA PIO driver
 *
 * Uses polling (BSY/DRQ status bits) — no interrupts, no DMA.
 * Supports LBA28 read/write and IDENTIFY for drive detection.
 *
 * ATA register map:
 *   Primary   channel: base 0x1F0, control 0x3F6
 *   Secondary channel: base 0x170, control 0x376
 *
 *   base+0  Data           (16-bit r/w)
 *   base+1  Error / Features
 *   base+2  Sector Count
 *   base+3  LBA Low   (bits  0-7)
 *   base+4  LBA Mid   (bits  8-15)
 *   base+5  LBA High  (bits 16-23)
 *   base+6  Drive/Head: bit7=1 bit6=LBA bit5=1 bit4=slave bit3-0=LBA[24-27]
 *   base+7  Status (r) / Command (w)
 */

#include "ata.h"
#include <stdint.h>

/* ── Port bases ──────────────────────────────────────────────────────────── */
static const uint16_t ATA_BASE[4]    = { 0x1F0, 0x1F0, 0x170, 0x170 };
static const uint16_t ATA_CTRL[4]    = { 0x3F6, 0x3F6, 0x376, 0x376 };
static const uint8_t  ATA_SLAVE[4]   = { 0, 1, 0, 1 };   /* drive select bit */

/* ATA status register bits */
#define ATA_SR_BSY  0x80    /* busy               */
#define ATA_SR_DRQ  0x08    /* data request ready */
#define ATA_SR_ERR  0x01    /* error              */
#define ATA_SR_DF   0x20    /* drive fault        */

/* ATA commands */
#define ATA_CMD_READ_SECTORS  0x20
#define ATA_CMD_WRITE_SECTORS 0x30
#define ATA_CMD_CACHE_FLUSH   0xE7
#define ATA_CMD_IDENTIFY      0xEC

/* IDENTIFY response word offsets (each word = 2 bytes, little-endian) */
#define ID_LBA28_SECTS_LO   60   /* words 60-61: LBA28 addressable sectors */
#define ID_LBA28_SECTS_HI   61
#define ID_LBA48_SECTS_0    100  /* words 100-103: LBA48 addressable sectors */
#define ID_LBA48_SECTS_1    101
#define ID_LBA48_SECTS_2    102
#define ID_LBA48_SECTS_3    103
#define ID_MODEL_FIRST      27   /* words 27-46: model string (byte-swapped) */
#define ID_MODEL_LAST       46

/* ── I/O helpers ─────────────────────────────────────────────────────────── */
static inline void outb(uint16_t port, uint8_t val) {
    __asm__ volatile ("outb %0,%1" :: "a"(val),"Nd"(port));
}
static inline uint8_t inb(uint16_t port) {
    uint8_t v;
    __asm__ volatile ("inb %1,%0" : "=a"(v) : "Nd"(port));
    return v;
}
static inline uint16_t inw(uint16_t port) {
    uint16_t v;
    __asm__ volatile ("inw %1,%0" : "=a"(v) : "Nd"(port));
    return v;
}
static inline void outw(uint16_t port, uint16_t val) {
    __asm__ volatile ("outw %0,%1" :: "a"(val),"Nd"(port));
}

/* Small busy-wait: read the alt-status register 4× to add ~400 ns delay */
static inline void ata_delay(uint8_t drv) {
    inb(ATA_CTRL[drv]);
    inb(ATA_CTRL[drv]);
    inb(ATA_CTRL[drv]);
    inb(ATA_CTRL[drv]);
}

/* Poll until BSY clears.  Returns 0 on success, -1 on timeout/error. */
static int ata_wait_not_busy(uint8_t drv) {
    uint32_t timeout = 100000;
    while (timeout--) {
        uint8_t s = inb(ATA_BASE[drv] + 7);
        if (!(s & ATA_SR_BSY)) return 0;
    }
    return -1;  /* timeout */
}

/* Poll until DRQ is set (and BSY clear).  Returns 0 OK, -1 error. */
static int ata_wait_drq(uint8_t drv) {
    uint32_t timeout = 100000;
    while (timeout--) {
        uint8_t s = inb(ATA_BASE[drv] + 7);
        if (s & ATA_SR_ERR) return -1;
        if (s & ATA_SR_DF)  return -1;
        if (!(s & ATA_SR_BSY) && (s & ATA_SR_DRQ)) return 0;
    }
    return -1;
}

/* ── Drive selection ─────────────────────────────────────────────────────── */
static void ata_select_drive(uint8_t drv, uint8_t lba_high4) {
    /* bit7=1, bit6=LBA, bit5=1, bit4=slave, bits3-0=LBA[27:24] */
    outb(ATA_BASE[drv] + 6,
         (uint8_t)(0xE0 | (ATA_SLAVE[drv] << 4) | (lba_high4 & 0x0F)));
    ata_delay(drv);
}

/* ── Public API ──────────────────────────────────────────────────────────── */

int ata_detect(ata_drive_t out[ATA_MAX_DRIVES]) {
    int count = 0;
    uint16_t id_buf[256];

    for (uint8_t d = 0; d < ATA_MAX_DRIVES; d++) {
        out[d].present       = ATA_NOT_PRESENT;
        out[d].drive_index   = d;
        out[d].total_sectors = 0;
        out[d].model[0]      = '\0';

        uint16_t base = ATA_BASE[d];

        /* Select drive */
        outb(base + 6, (uint8_t)(0xA0 | (ATA_SLAVE[d] << 4)));
        ata_delay(d);

        /* Check if drive responds at all — cylinder regs should be readable */
        outb(base + 2, 0);
        outb(base + 3, 0);
        outb(base + 4, 0);
        outb(base + 5, 0);

        /* Send IDENTIFY */
        outb(base + 7, ATA_CMD_IDENTIFY);
        ata_delay(d);

        /* Status = 0 means no drive */
        if (inb(base + 7) == 0) continue;

        /* Wait for BSY to clear */
        if (ata_wait_not_busy(d) < 0) continue;

        /* Check LBA mid/high — non-zero means ATAPI or non-ATA device */
        if (inb(base + 4) != 0 || inb(base + 5) != 0) continue;

        /* Wait for DRQ */
        if (ata_wait_drq(d) < 0) continue;

        /* Read 256 words of IDENTIFY data */
        for (int i = 0; i < 256; i++)
            id_buf[i] = inw(base);

        /* Extract model string (words 27-46, byte-swapped) */
        int mi = 0;
        for (int w = ID_MODEL_FIRST; w <= ID_MODEL_LAST; w++) {
            out[d].model[mi++] = (char)(id_buf[w] >> 8);
            out[d].model[mi++] = (char)(id_buf[w] & 0xFF);
        }
        out[d].model[40] = '\0';
        /* Trim trailing spaces */
        for (int i = 39; i >= 0 && out[d].model[i] == ' '; i--)
            out[d].model[i] = '\0';

        /* Prefer LBA48 count (words 100-101 give the low 32 bits, enough for
         * drives up to 2 TB); fall back to LBA28 if LBA48 is not reported. */
        uint32_t lba48_lo =
            (uint32_t)id_buf[ID_LBA48_SECTS_0] |
            ((uint32_t)id_buf[ID_LBA48_SECTS_1] << 16);
        uint32_t lba28 =
            (uint32_t)id_buf[ID_LBA28_SECTS_LO] |
            ((uint32_t)id_buf[ID_LBA28_SECTS_HI] << 16);

        out[d].total_sectors = (lba48_lo > lba28) ? lba48_lo : lba28;

        out[d].present = ATA_PRESENT;
        count++;
    }
    return count;
}

int ata_read_sector(uint8_t drv, uint32_t lba, uint8_t *buf) {
    if (drv >= ATA_MAX_DRIVES) return -1;

    if (ata_wait_not_busy(drv) < 0) return -1;

    ata_select_drive(drv, (uint8_t)(lba >> 24));
    outb(ATA_BASE[drv] + 1, 0x00);                    /* features      */
    outb(ATA_BASE[drv] + 2, 1);                        /* sector count  */
    outb(ATA_BASE[drv] + 3, (uint8_t)(lba & 0xFF));
    outb(ATA_BASE[drv] + 4, (uint8_t)((lba >> 8)  & 0xFF));
    outb(ATA_BASE[drv] + 5, (uint8_t)((lba >> 16) & 0xFF));
    outb(ATA_BASE[drv] + 7, ATA_CMD_READ_SECTORS);

    if (ata_wait_drq(drv) < 0) return -1;

    uint16_t *w = (uint16_t *)buf;
    for (int i = 0; i < 256; i++)
        w[i] = inw(ATA_BASE[drv]);

    return 0;
}

int ata_write_sector(uint8_t drv, uint32_t lba, const uint8_t *buf) {
    if (drv >= ATA_MAX_DRIVES) return -1;

    if (ata_wait_not_busy(drv) < 0) return -1;

    ata_select_drive(drv, (uint8_t)(lba >> 24));
    outb(ATA_BASE[drv] + 1, 0x00);
    outb(ATA_BASE[drv] + 2, 1);
    outb(ATA_BASE[drv] + 3, (uint8_t)(lba & 0xFF));
    outb(ATA_BASE[drv] + 4, (uint8_t)((lba >> 8)  & 0xFF));
    outb(ATA_BASE[drv] + 5, (uint8_t)((lba >> 16) & 0xFF));
    outb(ATA_BASE[drv] + 7, ATA_CMD_WRITE_SECTORS);

    if (ata_wait_drq(drv) < 0) return -1;

    const uint16_t *w = (const uint16_t *)buf;
    for (int i = 0; i < 256; i++)
        outw(ATA_BASE[drv], w[i]);

    /* Flush write cache */
    outb(ATA_BASE[drv] + 7, ATA_CMD_CACHE_FLUSH);
    if (ata_wait_not_busy(drv) < 0) return -1;

    return 0;
}
