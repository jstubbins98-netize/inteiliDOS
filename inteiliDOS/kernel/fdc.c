/*
 * inteiliDOS -- kernel/fdc.c
 * Intel 8272A / 82077AA Floppy Disk Controller driver
 *
 * Drives the primary FDC in DMA mode (ISA DMA channel 2).
 * Compatible with the PIIX4E integrated FDC on the HP Vectra VEi8.
 *
 * 1.44 MB 3.5" geometry assumed:
 *   80 tracks  ×  2 heads  ×  18 sectors/track  =  2880 logical sectors
 *   512 bytes per sector, MFM, 500 Kbps data rate
 *
 * IRQ6 (the floppy interrupt) is masked at the 8259 PIC during all
 * FDC operations so that the lack of an IRQ6 handler does not cause a
 * fault.  Completion is detected by polling the FDC MSR.
 *
 * Edge-case hardening (real-hardware concerns):
 *
 *   Motor spin-up  — fdc_motor_start() always waits FDC_MOTOR_SPINUP_MS
 *                    (300 ms) after asserting the motor bit before issuing
 *                    any READ DATA command.  This matches the minimum
 *                    required by the 82077AA data sheet and avoids
 *                    reading from a not-yet-stable platter.
 *
 *   DMA alignment  — fdc_raw_buf[] is 1024 bytes.  A compile-time
 *                    _Static_assert verifies the size, and fdc_get_dma_ptr()
 *                    selects whichever 512-byte half does not cross a 64 KB
 *                    ISA DMA page boundary.  With a 1024-byte buffer at
 *                    least one half is always safe (if the first half
 *                    crosses a boundary, the second half begins at the next
 *                    512-byte-aligned address which cannot cross again).
 *
 *   Error codes    — fdc_read_sector() returns FDC_ERR_TIMEOUT (-2),
 *                    FDC_ERR_ABNORMAL (-3), or FDC_ERR_CRC (-4) for
 *                    distinct hardware failure modes instead of a generic
 *                    -1, letting callers present a meaningful diagnosis.
 *
 *   Auto-retry     — On FDC_ERR_ABNORMAL or FDC_ERR_CRC the driver issues
 *                    a soft-reset, recalibrates, and retries once before
 *                    returning the error to the caller.  Timeout errors are
 *                    not retried (the controller is unresponsive).
 */

#include "fdc.h"
#include "timer.h"
#include "vga.h"
#include <stdint.h>
#include <stddef.h>

/* ── Port I/O helpers (self-contained, like ata.c / cdrom.c) ──────────── */
static inline void outb(uint16_t port, uint8_t val) {
    __asm__ volatile ("outb %0,%1" :: "a"(val), "Nd"(port));
}
static inline uint8_t inb(uint16_t port) {
    uint8_t v;
    __asm__ volatile ("inb %1,%0" : "=a"(v) : "Nd"(port));
    return v;
}

/* ── FDC register addresses (primary controller) ─────────────────────── */
#define FDC_DOR  0x3F2u   /* Digital Output Register          (write)  */
#define FDC_MSR  0x3F4u   /* Main Status Register             (read)   */
#define FDC_FIFO 0x3F5u   /* Data FIFO                    (read/write) */
#define FDC_DIR  0x3F7u   /* Digital Input Register           (read)   */
#define FDC_CCR  0x3F7u   /* Configuration Control Register   (write)  */

/* MSR bits */
#define MSR_RQM  0x80u    /* Data register ready                      */
#define MSR_DIO  0x40u    /* Direction: 1=FDC→CPU  0=CPU→FDC          */
#define MSR_CB   0x10u    /* FDC command busy                         */

/* FDC commands */
#define CMD_SPECIFY      0x03u
#define CMD_SENSE_INT    0x08u
#define CMD_READ_DATA    0x46u   /* MFM=1, MT=0, SK=0, opcode 6       */
#define CMD_RECALIBRATE  0x07u
#define CMD_SEEK         0x0Fu

/* DMA registers (ISA master DMA, channels 0-3) */
#define DMA_ADDR_CH2     0x04u   /* address register (write lo then hi) */
#define DMA_COUNT_CH2    0x05u   /* count register   (write lo then hi) */
#define DMA_PAGE_CH2     0x81u   /* page register (bits 16-23)          */
#define DMA_SINGLE_MASK  0x0Au   /* single channel mask register        */
#define DMA_MODE         0x0Bu   /* mode register                       */
#define DMA_RESET_FF     0x0Cu   /* flip-flop reset (write any)         */

/* 8259 PIC master mask register */
#define PIC1_MASK 0x21u
#define IRQ6_BIT  0x40u   /* bit 6 of master mask = IRQ6 (floppy)     */

/*
 * Motor spin-up delay.
 *
 * The 82077AA data sheet requires at least 300 ms between asserting the
 * MOTEN bit in DOR and issuing the first READ DATA command so that the
 * disk is rotating at full speed and the head is settled.  This constant
 * is used explicitly in fdc_motor_start(); do not reduce it.
 */
#define FDC_MOTOR_SPINUP_MS  300u

/* ── DMA buffer ───────────────────────────────────────────────────────── */
/*
 * Must not cross a 64 KB ISA DMA boundary.  We allocate 1 KB so that
 * at least one 512-byte half is guaranteed to lie entirely within a
 * single 64 KB DMA page.
 *
 *   Proof: if the first half [A, A+512) crosses a boundary, then
 *   (A & 0xFFFF) > 0xFF00.  The second half starts at A+512 whose low
 *   16 bits are ((A & 0xFFFF) + 512) - 0x10000 < 512, so the second half
 *   [A+512, A+1024) fits well within the next 64 KB page.
 *
 * The _Static_assert below enforces the minimum buffer size at compile
 * time so that the runtime half-selection in fdc_get_dma_ptr() is always
 * guaranteed to find a valid region.
 */
static uint8_t fdc_raw_buf[1024];
_Static_assert(sizeof(fdc_raw_buf) >= 1024u,
    "fdc_raw_buf must be >= 1024 bytes to guarantee a 64 KB page-safe "
    "512-byte DMA region");

static uint8_t *fdc_get_dma_ptr(void) {
    uintptr_t addr = (uintptr_t)fdc_raw_buf;
    /* If the first 512 bytes would cross a 64 KB DMA page boundary,
     * use the second half instead.  See proof in the block comment above. */
    if (((addr & 0xFFFFu) + 512u) > 0x10000u)
        return fdc_raw_buf + 512;
    return fdc_raw_buf;
}

/* ── Per-drive state ──────────────────────────────────────────────────── */
static uint8_t fdc_cur_track[2] = { 0xFFu, 0xFFu };  /* 0xFF = unknown */
static uint8_t fdc_motor_on     = 0;                  /* bitmask        */

/* Last error recorded by fdc_read_sector (0 = success). */
static int fdc_last_err = 0;

/* ── Low-level FIFO access ────────────────────────────────────────────── */

/* Send one byte to the FDC FIFO (polling). */
static int fdc_send(uint8_t b) {
    for (int t = 0; t < 100000; t++) {
        uint8_t msr = inb(FDC_MSR);
        if ((msr & MSR_RQM) && !(msr & MSR_DIO)) {
            outb(FDC_FIFO, b);
            return 0;
        }
    }
    return -1;   /* timeout */
}

/* Read one byte from the FDC FIFO (polling). */
static int fdc_recv(void) {
    for (int t = 0; t < 100000; t++) {
        uint8_t msr = inb(FDC_MSR);
        if ((msr & MSR_RQM) && (msr & MSR_DIO))
            return (int)(unsigned int)inb(FDC_FIFO);
    }
    return -1;   /* timeout */
}

/* ── SENSE INTERRUPT STATUS ───────────────────────────────────────────── */
static void fdc_sense_irq(uint8_t *st0_out, uint8_t *pcn_out) {
    fdc_send(CMD_SENSE_INT);
    int st0 = fdc_recv();
    int pcn = fdc_recv();
    if (st0_out) *st0_out = (uint8_t)(st0 < 0 ? 0 : st0);
    if (pcn_out) *pcn_out = (uint8_t)(pcn < 0 ? 0 : pcn);
}

/* ── Motor control ────────────────────────────────────────────────────── */
static void fdc_motor_start(uint8_t drive) {
    uint8_t dor = (uint8_t)(0x0Cu | (1u << (4 + drive)) | (drive & 0x03u));
    outb(FDC_DOR, dor);
    if (!(fdc_motor_on & (1u << drive))) {
        /*
         * Explicit spin-up delay: the 82077AA data sheet requires at least
         * 300 ms between asserting the motor-on bit and issuing the first
         * READ DATA command.  Skipping or reducing this delay causes silent
         * misreads on real hardware because the platter has not yet reached
         * its rated speed.
         */
        timer_sleep(FDC_MOTOR_SPINUP_MS);
        fdc_motor_on |= (uint8_t)(1u << drive);
    }
}

/* ── Reset ────────────────────────────────────────────────────────────── */
static void fdc_reset(void) {
    uint8_t old_mask = inb(PIC1_MASK);
    outb(PIC1_MASK, (uint8_t)(old_mask | IRQ6_BIT));  /* mask IRQ6 */

    outb(FDC_DOR, 0x00);    /* assert reset (NRESET=0)              */
    timer_sleep(5);
    outb(FDC_DOR, 0x0C);    /* deassert reset, DMA enabled, no motor */
    timer_sleep(20);

    /* Issue 4 × SENSE INTERRUPT STATUS to clear the reset-generated
     * interrupts for each of the 4 logical drives.                   */
    for (int i = 0; i < 4; i++) {
        uint8_t st0, pcn;
        fdc_sense_irq(&st0, &pcn);
    }

    fdc_motor_on  = 0;
    fdc_cur_track[0] = 0xFFu;
    fdc_cur_track[1] = 0xFFu;

    outb(PIC1_MASK, old_mask);   /* restore IRQ6 mask */
}

/* ── SPECIFY ──────────────────────────────────────────────────────────── */
static void fdc_specify(void) {
    /*
     * At 500 Kbps (1.44 MB data rate):
     *   SRT  = 0xD  → 3 ms step rate
     *   HUT  = 0xF  → 240 ms head unload time
     *   HLT  = 0x1  → 4 ms head load time
     *   NDMA = 0    → DMA mode
     */
    fdc_send(CMD_SPECIFY);
    fdc_send(0xDFu);   /* (SRT << 4) | HUT */
    fdc_send(0x02u);   /* (HLT << 1) | NDMA */
}

/* ── RECALIBRATE ──────────────────────────────────────────────────────── */
static int fdc_recalibrate(uint8_t drive) {
    uint8_t old_mask = inb(PIC1_MASK);
    outb(PIC1_MASK, (uint8_t)(old_mask | IRQ6_BIT));

    fdc_send(CMD_RECALIBRATE);
    fdc_send(drive & 0x03u);

    /* Recalibrate moves up to 80 tracks; worst case ~480 ms. */
    timer_sleep(500);

    uint8_t st0, pcn;
    fdc_sense_irq(&st0, &pcn);

    outb(PIC1_MASK, old_mask);

    fdc_cur_track[drive & 1u] = 0;

    /* ST0 bits [6:5] = 0b10 means abnormal termination. */
    return ((st0 & 0xC0u) == 0x00u || (st0 & 0xC0u) == 0x40u) ? 0 : -1;
}

/* ── SEEK ─────────────────────────────────────────────────────────────── */
static int fdc_seek(uint8_t drive, uint8_t head, uint8_t track) {
    if (fdc_cur_track[drive & 1u] == track)
        return 0;   /* already there */

    uint8_t old_mask = inb(PIC1_MASK);
    outb(PIC1_MASK, (uint8_t)(old_mask | IRQ6_BIT));

    fdc_send(CMD_SEEK);
    fdc_send((uint8_t)((head << 2) | (drive & 0x03u)));
    fdc_send(track);

    /* Allow enough time for the seek to complete. */
    timer_sleep(50);

    uint8_t st0, pcn;
    fdc_sense_irq(&st0, &pcn);

    outb(PIC1_MASK, old_mask);

    fdc_cur_track[drive & 1u] = track;
    return ((st0 & 0xC0u) == 0x00u || (st0 & 0x20u)) ? 0 : -1;
}

/* ── DMA setup for channel 2 (floppy read: device → memory) ──────────── */
static void fdc_dma_setup_read(const uint8_t *buf, uint16_t count) {
    uintptr_t addr = (uintptr_t)buf;
    uint8_t page  = (uint8_t)(addr >> 16);
    uint8_t lo    = (uint8_t)(addr & 0xFFu);
    uint8_t hi    = (uint8_t)((addr >> 8) & 0xFFu);
    uint16_t cnt  = (uint16_t)(count - 1u);

    outb(DMA_SINGLE_MASK, 0x06u);       /* mask channel 2               */
    outb(DMA_RESET_FF,    0xFFu);       /* reset flip-flop              */
    outb(DMA_ADDR_CH2, lo);             /* address low byte             */
    outb(DMA_ADDR_CH2, hi);             /* address high byte            */
    outb(DMA_PAGE_CH2, page);           /* address bits 16-23           */
    outb(DMA_RESET_FF, 0xFFu);          /* reset flip-flop again        */
    outb(DMA_COUNT_CH2, (uint8_t)(cnt & 0xFFu));       /* count lo      */
    outb(DMA_COUNT_CH2, (uint8_t)((cnt >> 8) & 0xFFu));/* count hi      */
    outb(DMA_MODE, 0x46u);              /* single, write-to-mem, ch2    */
    outb(DMA_SINGLE_MASK, 0x02u);       /* unmask channel 2             */
}

/* ── Wait for READ DATA result phase ─────────────────────────────────── */
static int fdc_wait_result(void) {
    /* Poll MSR until RQM=1 and DIO=1 (FDC entering result phase). */
    for (int t = 0; t < 2000000; t++) {
        uint8_t msr = inb(FDC_MSR);
        if ((msr & (MSR_RQM | MSR_DIO)) == (MSR_RQM | MSR_DIO))
            return 0;
    }
    return -1;  /* timeout (~2 s) */
}

/* ── Single read attempt ──────────────────────────────────────────────── */
/*
 * fdc_do_read_attempt — issue one READ DATA command and collect the result.
 *
 * Preconditions: motor is on and spinning, head is positioned on the
 * correct track (fdc_seek has been called), DMA is not yet programmed.
 *
 * Returns:
 *   0                on success (sector data in *dma, 512 bytes)
 *   FDC_ERR_TIMEOUT  if the FDC does not enter result phase
 *   FDC_ERR_ABNORMAL if ST0 bits [7:6] == 10b
 *   FDC_ERR_CRC      if ST1 bit 5 or ST2 bit 5 is set
 */
static int fdc_do_read_attempt(uint8_t drive, uint8_t head,
                                uint8_t track, uint8_t sector,
                                uint8_t *dma) {
    uint8_t old_mask = inb(PIC1_MASK);
    outb(PIC1_MASK, (uint8_t)(old_mask | IRQ6_BIT));  /* mask IRQ6 */

    fdc_dma_setup_read(dma, 512);

    /* Issue READ DATA command (9 parameter bytes). */
    fdc_send(CMD_READ_DATA);
    fdc_send((uint8_t)((head << 2) | (drive & 0x03u)));
    fdc_send(track);
    fdc_send(head);
    fdc_send(sector);
    fdc_send(0x02u);    /* N = 2 → 512 bytes/sector                   */
    fdc_send(18u);      /* EOT = last sector on track                  */
    fdc_send(0x1Bu);    /* GPL = gap 3 for 1.44 MB MFM                */
    fdc_send(0xFFu);    /* DTL = unused when N != 0                    */

    /* Wait for the FDC to enter result phase (DMA transfer complete). */
    int rc = fdc_wait_result();

    /* Read all 7 result bytes regardless, to release the FDC FIFO. */
    int st0 = fdc_recv();
    int st1 = fdc_recv();
    int st2 = fdc_recv();
    /* C, H, R, N — positioning info, not needed for error decode */
    fdc_recv(); fdc_recv(); fdc_recv(); fdc_recv();

    outb(PIC1_MASK, old_mask);

    if (rc != 0)
        return FDC_ERR_TIMEOUT;

    if (st0 < 0)
        return FDC_ERR_TIMEOUT;   /* FIFO timeout during result phase */

    /* ST0 bits [7:6] = 10b → abnormal termination (no disk, wrong addr) */
    if ((uint8_t)st0 & 0x80u)
        return FDC_ERR_ABNORMAL;

    /* ST1 bit 5 = CRC error in ID field; ST2 bit 5 = CRC in data field */
    if (((uint8_t)st1 & 0x20u) || ((uint8_t)st2 & 0x20u))
        return FDC_ERR_CRC;

    /* ST1 bit 7 = end-of-cylinder; treat as abnormal termination */
    if ((uint8_t)st1 & 0x80u)
        return FDC_ERR_ABNORMAL;

    (void)st2;
    return 0;   /* success */
}

/* ── Public API ───────────────────────────────────────────────────────── */

int fdc_init(void) {
    /* Set 500 Kbps data rate for 1.44 MB disks. */
    outb(FDC_CCR, 0x00u);

    fdc_reset();
    fdc_specify();

    /* Recalibrate drive A: to verify a controller is present. */
    fdc_motor_start(0);
    int rc = fdc_recalibrate(0);
    /* Leave motor running; it will be turned off by fdc_read_sector when
     * done or by a future fdc_motor_off call.                            */
    return rc;
}

int fdc_read_sector(uint8_t drive, uint32_t lba, uint8_t *buf) {
    if (drive > 1 || lba >= 2880u || !buf) {
        fdc_last_err = -1;
        return -1;
    }

    /* LBA → CHS for 1.44 MB (18 sectors/track, 2 heads, 80 tracks) */
    uint8_t track  = (uint8_t)(lba / 36u);
    uint8_t head   = (uint8_t)((lba / 18u) & 1u);
    uint8_t sector = (uint8_t)((lba % 18u) + 1u);

    /*
     * Motor spin-up is enforced inside fdc_motor_start(): the function
     * waits FDC_MOTOR_SPINUP_MS (300 ms) the first time the motor is
     * switched on, guaranteeing the platter is at full speed before the
     * READ DATA command is issued below.
     */
    fdc_motor_start(drive);

    if (fdc_seek(drive, head, track) != 0) {
        fdc_last_err = -1;
        return -1;
    }

    uint8_t *dma = fdc_get_dma_ptr();

    /* ── First attempt ── */
    int rc = fdc_do_read_attempt(drive, head, track, sector, dma);

    /*
     * Soft-reset + retry on recoverable hardware errors.
     *
     * FDC_ERR_ABNORMAL and FDC_ERR_CRC can be caused by a transient head
     * settle glitch or a marginal sector.  A full soft-reset clears any
     * internal FDC state, and recalibrating ensures the track register is
     * correct before the second attempt.  Only one retry is performed;
     * persistent errors are returned to the caller unchanged.
     *
     * FDC_ERR_TIMEOUT is not retried: if the controller is unresponsive
     * a second command will also time out and merely waste ~4 seconds.
     */
    if (rc == FDC_ERR_ABNORMAL || rc == FDC_ERR_CRC) {
        /* Soft-reset and re-initialise the controller. */
        fdc_reset();
        fdc_specify();

        /* Re-spin the motor (reset cleared the DOR motor bit). */
        fdc_motor_start(drive);

        /* Recalibrate so the FDC track register is in a known state. */
        if (fdc_recalibrate(drive) != 0) {
            fdc_last_err = -1;
            return -1;
        }

        /* Re-seek to the target track. */
        fdc_cur_track[drive & 1u] = 0xFFu;   /* force seek */
        if (fdc_seek(drive, head, track) != 0) {
            fdc_last_err = -1;
            return -1;
        }

        /* ── Retry attempt ── */
        rc = fdc_do_read_attempt(drive, head, track, sector, dma);
    }

    fdc_last_err = rc;

    if (rc != 0)
        return rc;   /* FDC_ERR_TIMEOUT / FDC_ERR_ABNORMAL / FDC_ERR_CRC */

    /* Copy DMA buffer into caller's buffer. */
    for (int i = 0; i < 512; i++)
        buf[i] = dma[i];

    return 0;
}

int fdc_last_error_get(void) {
    return fdc_last_err;
}
