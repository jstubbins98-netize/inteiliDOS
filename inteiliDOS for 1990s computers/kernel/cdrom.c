/*
 * inteiliDOS -- kernel/cdrom.c
 * ATAPI CD-ROM / DVD-ROM driver (PIO, no DMA)
 *
 * Protocol summary:
 *   1. Select the drive with the drive/head register.
 *   2. Set Features=0 (PIO transfer), Byte Count limit in base+4/+5.
 *   3. Issue ATA command PACKET (0xA0).
 *   4. Wait for DRQ — the drive is now ready to accept a 12-byte CDB.
 *   5. Write the 12-byte Command Descriptor Block as 6 × 16-bit words.
 *   6. For data-IN transfers: wait for DRQ again, read back the data.
 *   7. Wait for BSY to clear before issuing the next command.
 *
 * IDENTIFY PACKET DEVICE returns the device type in identify word 0.
 * The 0x14/0xEB cylinder signature belongs to reset/aborted ATA IDENTIFY,
 * not successful packet identification; it may already have been overwritten
 * by BIOS CD reads (notably on 86Box).
 *
 * All I/O is 16-bit PIO.  No DMA, no IRQ — purely polling.
 * The driver is self-contained: it re-declares the port-I/O inlines rather
 * than sharing ata.c's statics, so the two files compile independently.
 */

#include "cdrom.h"
#include "ata.h"
#include "vga.h"
#include <stdint.h>

/* ── IDE port bases (indexed 0..3 = pri-master, pri-slave, sec-master, sec-slave) */
static uint16_t BASE[4] = { 0x1F0, 0x1F0, 0x170, 0x170 };
static uint16_t CTRL[4] = { 0x3F6, 0x3F6, 0x376, 0x376 };
static const uint8_t  SLAV[4] = { 0,     1,     0,     1     };

/* ATA / ATAPI status register bits */
#define SR_BSY   0x80   /* controller busy                       */
#define SR_DRQ   0x08   /* data request — drive wants a transfer */
#define SR_ERR   0x01   /* error bit                             */
#define SR_DF    0x20   /* drive fault                           */

/* ATA commands */
#define ATA_IDENTIFY_PACKET  0xA1   /* IDENTIFY PACKET DEVICE   */
#define ATA_PACKET           0xA0   /* PACKET (tunnels a CDB)   */

/* SCSI/MMC Command Descriptor Block opcodes (12-byte format) */
#define CDB_TEST_UNIT_READY  0x00
#define CDB_REQUEST_SENSE    0x03
#define CDB_READ_CAPACITY10  0x25   /* READ CAPACITY (10)       */
#define CDB_READ10           0x28   /* READ (10)                */
#define CDB_START_STOP_UNIT  0x1B   /* START STOP UNIT          */

/* Module-level drive table, populated by cdrom_init() */
static cdrom_drive_t cdrom_tbl[CDROM_MAX_DRIVES];
static int           cdrom_cnt = 0;

/* ── Inline port I/O helpers ──────────────────────────────────────────────── */

#ifdef CDROM_TEST_IO
#include "cdrom_test_io.h"
#else
static inline void outb(uint16_t port, uint8_t val) {
    __asm__ volatile ("outb %0,%1" :: "a"(val), "Nd"(port));
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
    __asm__ volatile ("outw %0,%1" :: "a"(val), "Nd"(port));
}
#endif

/*
 * atapi_delay — read the alt-status register 4× to produce a ≥ 400 ns delay.
 * Required after drive-select writes before reading status.
 */
static inline void atapi_delay(uint8_t d) {
    inb(CTRL[d]); inb(CTRL[d]); inb(CTRL[d]); inb(CTRL[d]);
}

/* ── Polling primitives ───────────────────────────────────────────────────── */

/*
 * wait_not_busy — spin until the BSY bit clears.
 * Returns 0 on success, -1 on timeout (~500 ms at 1 GHz).
 */
static int wait_not_busy(uint8_t d) {
    uint32_t t = 5000000;
    while (t--) {
        if (!(inb(BASE[d] + 7) & SR_BSY)) return 0;
    }
    return -1;
}

/*
 * wait_drq — spin until DRQ is set and BSY is clear, or an error bit fires.
 * Returns 0 on success, -1 on error or timeout.
 */
static int wait_drq(uint8_t d) {
    uint32_t t = 5000000;
    while (t--) {
        uint8_t s = inb(BASE[d] + 7);
        /* Other status bits are undefined while BSY is asserted. */
        if (s & SR_BSY) continue;
        if (s & SR_ERR) return -1;
        if (s & SR_DF)  return -1;
        if (!(s & SR_BSY) && (s & SR_DRQ)) return 0;
    }
    return -1;
}

/* ── PACKET command dispatcher ────────────────────────────────────────────── */

/*
 * atapi_packet — send a 12-byte CDB and (optionally) receive data.
 *
 *   d          : drive index 0–3
 *   cdb        : 12-byte Command Descriptor Block
 *   buf        : destination buffer for data-IN phase (NULL = no data)
 *   max_bytes  : maximum bytes to receive (0 = no data)
 *
 * Returns 0 on success, -1 on any error.
 *
 * The transfer size programmed into the Byte Count registers is max_bytes.
 * After DRQ in the data phase the controller reports how many bytes are
 * actually available in base+4/+5; we use that actual count to avoid
 * reading past the real data.
 */
static int atapi_packet(uint8_t d, const uint8_t *cdb,
                         uint8_t *buf, uint16_t max_bytes) {
    if (d >= CDROM_MAX_DRIVES) return -1;

    /* Drive select: bit7=1, bit6=0 (LBA), bit5=1, bit4=slave, bits3-0=0 */
    outb(BASE[d] + 6, (uint8_t)(0xA0 | (SLAV[d] << 4)));
    atapi_delay(d);

    if (wait_not_busy(d) < 0) return -1;

    /* Set up transfer */
    outb(BASE[d] + 1, 0x00);                             /* Features = 0 (PIO) */
    uint16_t limit = max_bytes ? max_bytes : CDROM_SECTOR_SIZE;
    outb(BASE[d] + 4, (uint8_t)(limit & 0xFF));
    outb(BASE[d] + 5, (uint8_t)(limit >> 8));
    outb(BASE[d] + 7, ATA_PACKET);                       /* PACKET command     */

    /* Device asserts DRQ when ready for the CDB */
    if (wait_drq(d) < 0) return -1;
    /* CoD=1, IO=0: the DRQ is for a command packet, not leftover data. */
    if ((inb(BASE[d] + 2) & 3) != 1) return -1;

    /* Write the 12-byte CDB as 6 × 16-bit words (low byte first) */
    for (int i = 0; i < 6; i++)
        outw(BASE[d], (uint16_t)((uint16_t)cdb[i * 2]
                               | ((uint16_t)cdb[i * 2 + 1] << 8)));

    atapi_delay(d);
    uint32_t received = 0;
    /* Bound the entire command, including controllers stuck in DRQ. */
    for (uint32_t polls = 0; polls < 5000000; polls++) {
        uint8_t status = inb(BASE[d] + 7);
        if (status & SR_BSY) continue;
        if (status & (SR_ERR | SR_DF)) return -1;
        uint8_t reason = inb(BASE[d] + 2) & 3;
        if (!(status & SR_DRQ)) {
            /* CoD=1, IO=1 indicates command completion. DRQ may be
             * temporarily clear between data phases, so don't stop early. */
            if (reason != 3) continue;
            return received == max_bytes ? 0 : -1;
        }
        if (reason == 1) continue; /* delayed transition after writing CDB */
        if (reason != 2) return -1; /* unexpected data-OUT phase */

        uint32_t actual = (uint16_t)((uint16_t)inb(BASE[d] + 4)
                                  | ((uint16_t)inb(BASE[d] + 5) << 8));
        if (actual == 0) actual = 65536; /* ATAPI zero count means 64 KiB */
        for (uint32_t i = 0; i < actual; i += 2) {
            uint16_t word = inw(BASE[d]);
            /* Drain the complete phase even if it exceeds the buffer. */
            if (buf && received + i < max_bytes)
                buf[received + i] = (uint8_t)word;
            if (buf && i + 1 < actual && received + i + 1 < max_bytes)
                buf[received + i + 1] = (uint8_t)(word >> 8);
        }
        received += actual;
        atapi_delay(d);
        if (received > 65536) return -1;
    }
    return -1;
}

/* Disc insertion/boot can leave UNIT ATTENTION pending. Clear it with
 * REQUEST SENSE and retry capacity, without treating an empty tray as absent. */
static int read_capacity(uint8_t d, uint8_t cap[8]) {
    const uint8_t cdb[12] = { CDB_READ_CAPACITY10 };
    const uint8_t sense_cdb[12] = { CDB_REQUEST_SENSE, 0, 0, 0, 18 };
    for (int attempt = 0; attempt < 3; attempt++) {
        if (atapi_packet(d, cdb, cap, 8) == 0) return 0;
        uint8_t sense[18] = {0};
        if (atapi_packet(d, sense_cdb, sense, 18) < 0) return -1;
        if ((sense[2] & 0x0F) != 6) return -1; /* not UNIT ATTENTION */
    }
    return -1;
}

/* ── Public API ───────────────────────────────────────────────────────────── */

int cdrom_detect(cdrom_drive_t out[CDROM_MAX_DRIVES]) {
    int count = 0;
    uint16_t id_buf[256];   /* IDENTIFY PACKET DEVICE response (unused; flushed) */

    for (uint8_t d = 0; d < CDROM_MAX_DRIVES; d++) {
        out[d].present     = CDROM_NOT_PRESENT;
        out[d].drive_index = d;
        out[d].last_lba    = 0;
        out[d].block_size  = CDROM_SECTOR_SIZE;
        if (ata_get_pata_ports(d, &BASE[d], &CTRL[d]) < 0) continue;

        /* Select drive */
        outb(BASE[d] + 6, (uint8_t)(0xA0 | (SLAV[d] << 4)));
        atapi_delay(d);

        /*
         * Wait for BSY=0 before sending any command.
         *
         * On boot the drives can still be finishing their internal
         * diagnostics by the time we reach here.  Without this wait,
         * IDENTIFY PACKET DEVICE is sent while BSY=1 and silently ignored;
         * the subsequent wait_not_busy then times out and the CD-ROM goes
         * undetected even though it is physically present.
         *
         * Status = 0xFF means nothing is on the bus; 0x00 after BSY was
         * set usually means the drive finished but left a zero-status
         * (some ATAPI drives do this briefly).  We re-read below after the
         * command to distinguish these cases properly.
         */
        uint8_t pre = inb(BASE[d] + 7);
        if (pre == 0xFF) continue;   /* floating bus — no device */
        if (pre != 0x00) {
            /* BSY might be set — wait for it to clear before commanding */
            if (wait_not_busy(d) < 0) continue;
        }

        /* Issue IDENTIFY PACKET DEVICE */
        outb(BASE[d] + 7, ATA_IDENTIFY_PACKET);
        atapi_delay(d);

        /* Status = 0 → nothing attached */
        uint8_t status = inb(BASE[d] + 7);
        if (status == 0x00) continue;

        /* Wait for BSY to clear after the command */
        if (wait_not_busy(d) < 0) continue;

        /* Successful A1 returns identity data, not a reset signature.
         * Read all 512 bytes to clear DRQ before issuing PACKET. */
        if (wait_drq(d) < 0) continue;
        for (int i = 0; i < 256; i++)
            id_buf[i] = inw(BASE[d]);
        atapi_delay(d);
        /* Word 0: packet device (bits 15:14=10), CD/DVD type (12:8=5),
         * 12-byte packets (1:0=0). Don't count failed identifies or HDDs. */
        if ((id_buf[0] & 0xDF03) != 0x8500) continue;

        out[d].present = CDROM_PRESENT;
        count++;

        /* ---- READ CAPACITY (10) — get disc geometry ---- */
        /*
         * The 8-byte response is:
         *   bytes 0-3 : LBA of the last addressable block (big-endian)
         *   bytes 4-7 : block length in bytes (big-endian; should be 2048)
         */
        uint8_t cap_buf[8] = {0};

        if (read_capacity(d, cap_buf) == 0) {
            out[d].last_lba =
                ((uint32_t)cap_buf[0] << 24) | ((uint32_t)cap_buf[1] << 16) |
                ((uint32_t)cap_buf[2] <<  8) |  (uint32_t)cap_buf[3];
            out[d].block_size =
                ((uint32_t)cap_buf[4] << 24) | ((uint32_t)cap_buf[5] << 16) |
                ((uint32_t)cap_buf[6] <<  8) |  (uint32_t)cap_buf[7];
            /* Sanity-check: drives should always report 2048 */
            if (out[d].block_size == 0 || out[d].block_size > 4096)
                out[d].block_size = CDROM_SECTOR_SIZE;
        }
        /* If READ CAPACITY fails (no disc inserted), last_lba stays 0 */
    }
    return count;
}

int cdrom_read_sector(uint8_t drive_index, uint32_t lba, uint8_t *buf) {
    if (drive_index >= CDROM_MAX_DRIVES) return -1;
    if (buf == (uint8_t *)0)            return -1;

    /*
     * READ (10) Command Descriptor Block — 10 bytes + 2 bytes zero padding.
     *
     *  Byte  0    : opcode (0x28)
     *  Byte  1    : flags  (bit 3 = FUA, bit 1 = RelAdr; both 0 here)
     *  Bytes 2–5  : LBA, big-endian
     *  Byte  6    : reserved / group number
     *  Bytes 7–8  : transfer length (number of blocks), big-endian = 1
     *  Byte  9    : control (0x00)
     *  Bytes 10–11: padding (ATAPI CDBs are always 12 bytes)
     */
    uint8_t cdb[12] = {
        CDB_READ10,            /* 0x28 */
        0x00,                  /* flags */
        (uint8_t)(lba >> 24), /* LBA[31:24] */
        (uint8_t)(lba >> 16), /* LBA[23:16] */
        (uint8_t)(lba >>  8), /* LBA[15:8]  */
        (uint8_t)(lba >>  0), /* LBA[7:0]   */
        0x00,                  /* reserved   */
        0x00,                  /* xfer_len high = 0 */
        0x01,                  /* xfer_len low  = 1 sector */
        0x00,                  /* control */
        0x00, 0x00             /* pad to 12 bytes */
    };

    return atapi_packet(drive_index, cdb, buf, CDROM_SECTOR_SIZE);
}

int cdrom_eject(uint8_t drive_index) {
    if (drive_index >= CDROM_MAX_DRIVES) return -1;

    /*
     * START STOP UNIT:
     *   Byte 0 : opcode (0x1B)
     *   Byte 1 : Immed=0 (wait for completion)
     *   Byte 4 : bits: LoEj (bit 1) | Start (bit 0)
     *            0x02 = LoEj=1, Start=0 → eject tray
     *
     * No data phase — pass NULL/0.
     */
    uint8_t cdb[12] = {
        CDB_START_STOP_UNIT,   /* 0x1B */
        0x00,                  /* Immed=0: wait for completion */
        0x00, 0x00,
        0x02,                  /* LoEj=1, Start=0 → eject */
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
    };

    return atapi_packet(drive_index, cdb, (uint8_t *)0, 0);
}

void cdrom_init(void) {
    cdrom_cnt = cdrom_detect(cdrom_tbl);
}

int cdrom_count(void) {
    return cdrom_cnt;
}

const cdrom_drive_t *cdrom_drives(void) {
    return cdrom_tbl;
}

uint32_t cdrom_rescan_media(uint8_t drive_index) {
    if (drive_index >= CDROM_MAX_DRIVES) return 0;
    if (cdrom_tbl[drive_index].present != CDROM_PRESENT) return 0;

    /* Reset last_lba — if READ CAPACITY fails the disc is absent. */
    cdrom_tbl[drive_index].last_lba   = 0;
    cdrom_tbl[drive_index].block_size = CDROM_SECTOR_SIZE;

    uint8_t cap_buf[8] = {0};
    if (read_capacity(drive_index, cap_buf) == 0) {
        uint32_t lba =
            ((uint32_t)cap_buf[0] << 24) | ((uint32_t)cap_buf[1] << 16) |
            ((uint32_t)cap_buf[2] <<  8) |  (uint32_t)cap_buf[3];
        uint32_t bsz =
            ((uint32_t)cap_buf[4] << 24) | ((uint32_t)cap_buf[5] << 16) |
            ((uint32_t)cap_buf[6] <<  8) |  (uint32_t)cap_buf[7];
        if (bsz == 0 || bsz > 4096) bsz = CDROM_SECTOR_SIZE;
        cdrom_tbl[drive_index].last_lba   = lba;
        cdrom_tbl[drive_index].block_size = bsz;
    }

    return cdrom_tbl[drive_index].last_lba;
}

void cdrom_rescan_all(void) {
    for (uint8_t d = 0; d < CDROM_MAX_DRIVES; d++)
        if (cdrom_tbl[d].present == CDROM_PRESENT)
            cdrom_rescan_media(d);
}
