/*
 * inteilidOS -- kernel/ata.c
 * ATA storage driver: PATA/IDE (legacy port I/O) and SATA/AHCI (MMIO).
 *
 * HP Vectra VEi8 hardware note (Intel 440BX / PIIX4E south bridge):
 *   PCI vendor 0x8086, device 0x7111 — Intel 82371AB/EB PIIX4E IDE
 *   The BIOS configures PIIX4E in compatibility mode (prog_if bits 0 and 2
 *   clear), so both IDE channels use the standard legacy I/O addresses:
 *     Primary   channel: cmd=0x1F0  ctl=0x3F6  IRQ 14
 *     Secondary channel: cmd=0x170  ctl=0x376  IRQ 15
 *   No explicit PCI initialisation is required beyond what the BIOS does;
 *   pata_probe_pci_ports() detects if native mode is active and adjusts
 *   the base addresses from the BARs in that case.
 *
 *   Timing: the PIIX4E's ATA timing registers default to Mode 0 PIO.
 *   Our polling loop (pata_wait_busy / pata_wait_drq) with the 400 ns
 *   I/O delay (pata_delay reads alt-status 4×) is sufficient for any
 *   UDMA/PIO-capable drive of the era (IBM Deskstar, Seagate Medalist, etc.).
 *   Each ata_write_sector call issues ATA_CMD_FLUSH (0xE7) and waits for
 *   BSY to clear before returning, ensuring data is committed to platter
 *   media before the installer reports success.
 *
 * PATA path  — polling PIO via legacy I/O ports 0x1F0 / 0x170.
 *              Drives 0-3 (primary master/slave, secondary master/slave).
 *
 * AHCI path  — MMIO via the HBA located through PCI class 01h/subclass 06h.
 *              Up to four AHCI ports with drives are assigned indices 4-7.
 *              Uses a single command slot (slot 0) per port, no NCQ, no DMA
 *              scatter-gather beyond the single 512-byte sector buffer.
 *
 * Both paths share the same public API: ata_detect / ata_read_sector /
 * ata_write_sector.  A module-level drive table (g_drives[]) carries the
 * per-drive bus information needed by the read/write dispatch.
 *
 * Memory model: inteiliDOS runs in 32-bit flat protected mode without
 * paging, so virtual address == physical address throughout.
 */

#include "ata.h"
#include "pci.h"
#include <stdint.h>

/* ═══════════════════════════════════════════════════════════════════════════
 * Shared I/O helpers
 * ═══════════════════════════════════════════════════════════════════════════ */

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

/* ═══════════════════════════════════════════════════════════════════════════
 * Module-level drive table
 * Populated by ata_detect(); read by ata_read_sector / ata_write_sector.
 * ═══════════════════════════════════════════════════════════════════════════ */

static ata_drive_t g_drives[ATA_MAX_DRIVES];

/* ═══════════════════════════════════════════════════════════════════════════
 * PATA/IDE — legacy port I/O path
 * ═══════════════════════════════════════════════════════════════════════════ */

/*
 * Port bases indexed by drive slot 0-3.
 * Non-const: pata_probe_pci_ports() may replace these with BARs from a
 * native-mode PCI IDE controller before the first probe.
 * Default values are the legacy fixed addresses that work in compatibility mode.
 */
static uint16_t g_ata_base[4] = { 0x1F0, 0x1F0, 0x170, 0x170 };
static uint16_t g_ata_ctrl[4] = { 0x3F6, 0x3F6, 0x376, 0x376 };
static const uint8_t ATA_SLAVE[4] = { 0, 1, 0, 1 };

/* ATA status bits */
#define ATA_SR_BSY  0x80
#define ATA_SR_DRQ  0x08
#define ATA_SR_ERR  0x01
#define ATA_SR_DF   0x20

/* ATA commands */
#define ATA_CMD_READ     0x20
#define ATA_CMD_WRITE    0x30
#define ATA_CMD_FLUSH    0xE7
#define ATA_CMD_IDENTIFY 0xEC

/* IDENTIFY word offsets */
#define ID_MODEL_FIRST     27
#define ID_MODEL_LAST      46
#define ID_LBA28_SECTS_LO  60
#define ID_LBA28_SECTS_HI  61
#define ID_LBA48_SECTS_0  100
#define ID_LBA48_SECTS_1  101

/* ~400 ns delay: read alt-status 4 times */
static inline void pata_delay(uint8_t d) {
    inb(g_ata_ctrl[d]); inb(g_ata_ctrl[d]);
    inb(g_ata_ctrl[d]); inb(g_ata_ctrl[d]);
}

/* Wait for BSY to clear.  Returns 0 OK, -1 timeout. */
static int pata_wait_busy(uint8_t d) {
    for (uint32_t t = 0; t < 100000; t++)
        if (!(inb(g_ata_base[d] + 7) & ATA_SR_BSY)) return 0;
    return -1;
}

/* Wait for DRQ (BSY clear, DRQ set).  Returns 0 OK, -1 error/timeout. */
static int pata_wait_drq(uint8_t d) {
    for (uint32_t t = 0; t < 100000; t++) {
        uint8_t s = inb(g_ata_base[d] + 7);
        if (s & (ATA_SR_ERR | ATA_SR_DF)) return -1;
        if (!(s & ATA_SR_BSY) && (s & ATA_SR_DRQ)) return 0;
    }
    return -1;
}

/* Select drive and load LBA[27:24] into drive/head register. */
static void pata_select(uint8_t d, uint8_t lba_hi4) {
    outb(g_ata_base[d] + 6,
         (uint8_t)(0xE0 | (ATA_SLAVE[d] << 4) | (lba_hi4 & 0x0F)));
    pata_delay(d);
}

/* Extract model string and sector count from a 256-word IDENTIFY buffer. */
static void pata_parse_identify(uint16_t *id, ata_drive_t *out) {
    /* Model string: words 27-46, byte-swapped */
    int mi = 0;
    for (int w = ID_MODEL_FIRST; w <= ID_MODEL_LAST; w++) {
        out->model[mi++] = (char)(id[w] >> 8);
        out->model[mi++] = (char)(id[w] & 0xFF);
    }
    out->model[40] = '\0';
    for (int i = 39; i >= 0 && out->model[i] == ' '; i--)
        out->model[i] = '\0';

    /* Prefer LBA48 sector count; fall back to LBA28 */
    uint32_t lba48_lo = (uint32_t)id[ID_LBA48_SECTS_0]
                      | ((uint32_t)id[ID_LBA48_SECTS_1] << 16);
    uint32_t lba28    = (uint32_t)id[ID_LBA28_SECTS_LO]
                      | ((uint32_t)id[ID_LBA28_SECTS_HI] << 16);
    out->total_sectors = (lba48_lo > lba28) ? lba48_lo : lba28;
}

/*
 * pata_probe_pci_ports — check for a PCI IDE controller (class 01/sub 01)
 * running in PCI native mode, and if found, read its I/O port addresses
 * from BAR0-3 instead of assuming the legacy 0x1F0/0x170 addresses.
 *
 * PCI IDE prog_if bits:
 *   bit 0 — primary channel in native PCI mode (ports from BAR0/BAR1)
 *   bit 2 — secondary channel in native PCI mode (ports from BAR2/BAR3)
 * When the bit is 0 the channel is in compatibility mode (0x1F0 / 0x170).
 *
 * I/O BARs have bit 0 set (PCI I/O space indicator); strip it to get the
 * actual port base.  BAR1/BAR3 (control ports) are 4 bytes wide but the
 * alt-status register sits at offset +2, so add 2.
 *
 * HP Vectra VEi8 / Intel 440BX PIIX4E (PCI 8086:7111):
 *   In the BIOS default configuration, prog_if bits 0 and 2 are both 0
 *   (compatibility mode).  This function finds the device and returns
 *   early without modifying g_ata_base[]/g_ata_ctrl[], leaving the
 *   standard 0x1F0/0x170 addresses in place — exactly what we want.
 */
static void pata_probe_pci_ports(void) {
    pci_device_t ide;
    if (pci_find_class(0x01, 0x01, &ide) != 0)
        return;     /* no PCI IDE controller — keep legacy defaults */

    uint8_t pi = ide.prog_if;

    /* Primary channel */
    if (pi & 0x01u) {
        uint16_t cmd = (uint16_t)(ide.bar[0] & ~1u);   /* strip I/O flag */
        uint16_t ctl = (uint16_t)((ide.bar[1] & ~1u) + 2u);
        if (cmd > 0x00FFu && ctl > 0x00FFu) {   /* sanity: above ISA space */
            g_ata_base[0] = g_ata_base[1] = cmd;
            g_ata_ctrl[0] = g_ata_ctrl[1] = ctl;
        }
    }

    /* Secondary channel */
    if (pi & 0x04u) {
        uint16_t cmd = (uint16_t)(ide.bar[2] & ~1u);
        uint16_t ctl = (uint16_t)((ide.bar[3] & ~1u) + 2u);
        if (cmd > 0x00FFu && ctl > 0x00FFu) {
            g_ata_base[2] = g_ata_base[3] = cmd;
            g_ata_ctrl[2] = g_ata_ctrl[3] = ctl;
        }
    }
}

/* Probe all four PATA positions.  Fills g_drives[0..3]. */
static int pata_detect(void) {
    uint16_t id_buf[256];
    int count = 0;

    /* Update port addresses from PCI BARs if controller is in native mode */
    pata_probe_pci_ports();

    /*
     * No software reset (SRST) here.
     *
     * When booting from the CD (GRUB on optical media), the drives are in a
     * clean idle state by the time the kernel runs.
     * When booting from the HDD (GRUB or VBR on the hard disk), GRUB / the
     * VBR just finished reading the kernel image — the drives are equally idle.
     *
     * Issuing SRST forces both drives on each channel through a hardware
     * diagnostic cycle that can take anywhere from 2 ms to > 400 ms (the
     * master waits for the slave's PDIAG signal with a 400 ms timeout before
     * giving up).  On HDD boot this diagnostic runs in a different BIOS
     * hardware state than on CD boot, causing BSY to stay asserted longer
     * than the polling loop expected and all drives to be skipped.
     *
     * SRST also leaves ATAPI drives (CD-ROMs) with an aborted IDENTIFY in
     * flight: pata_detect sends ATA IDENTIFY (0xEC) to the CD-ROM slot, the
     * drive aborts it, and on some PIIX4E firmware revisions the abort leaves
     * DRQ asserted with leftover status bytes.  cdrom_detect then finds the
     * drive mid-abort and its own IDENTIFY PACKET DEVICE fails too, making
     * both the HDD and CD-ROM invisible.
     *
     * The correct approach is to trust that the bootloader left the controller
     * in a usable state and just poll BSY=0 before each individual IDENTIFY.
     */

    for (uint8_t d = 0; d < 4; d++) {
        ata_drive_t *drv = &g_drives[d];
        drv->present      = ATA_NOT_PRESENT;
        drv->drive_type   = ATA_TYPE_PATA;
        drv->drive_index  = d;
        drv->total_sectors= 0;
        drv->model[0]     = '\0';
        drv->ahci_abar    = 0;
        drv->ahci_port    = 0;

        uint16_t base = g_ata_base[d];

        /* Select drive */
        outb(base + 6, (uint8_t)(0xA0 | (ATA_SLAVE[d] << 4)));
        pata_delay(d);

        /*
         * Pre-IDENTIFY bus-presence check.
         * No device: bus floats to 0xFF (pull-up) or is pulled to 0x00.
         * Either value means "nobody home" — skip before sending a command.
         */
        uint8_t st = inb(base + 7);
        if (st == 0xFF || st == 0x00) continue;

        /*
         * Wait for BSY=0 before sending any command.  The channel-wide SRST
         * above polls only the master; the slave may still be finishing its
         * post-SRST DIAG when we arrive here.  Sending IDENTIFY while BSY=1
         * causes the command to be silently ignored, pata_wait_busy then
         * times out, and the drive goes undetected.
         */
        if (pata_wait_busy(d) < 0) continue;

        /* Zero sector/cylinder regs so an ATAPI signature will show up */
        outb(base + 2, 0); outb(base + 3, 0);
        outb(base + 4, 0); outb(base + 5, 0);

        /* Send IDENTIFY */
        outb(base + 7, ATA_CMD_IDENTIFY);
        pata_delay(d);

        if (pata_wait_busy(d) < 0) continue;

        /* Non-zero LBA mid/high → ATAPI (CD-ROM etc.) — skip.
         *
         * When an ATAPI drive receives ATA IDENTIFY (0xEC) it aborts the
         * command and may leave DRQ asserted with abort-status bytes pending
         * in the data register.  If we just `continue` here without draining
         * those bytes, cdrom_detect will find the drive mid-abort and its own
         * IDENTIFY PACKET DEVICE probe will fail.  Read any pending words
         * until DRQ clears before moving on.
         */
        if (inb(base + 4) != 0 || inb(base + 5) != 0) {
            /* Drain up to one sector's worth of pending data (256 words). */
            for (int drain = 0; drain < 256; drain++) {
                uint8_t s = inb(base + 7);
                if (s & ATA_SR_BSY) break;   /* BSY re-asserted — stop     */
                if (!(s & ATA_SR_DRQ)) break; /* DRQ clear — bus is clean  */
                (void)inw(base);              /* discard the abort-status word */
            }
            continue;
        }

        if (pata_wait_drq(d) < 0) continue;

        for (int i = 0; i < 256; i++)
            id_buf[i] = inw(base);

        pata_parse_identify(id_buf, drv);
        drv->present = ATA_PRESENT;
        count++;
    }
    return count;
}

/* PATA read — called from ata_read_sector when drive_type == ATA_TYPE_PATA */
static int pata_read(uint8_t d, uint32_t lba, uint8_t *buf) {
    if (pata_wait_busy(d) < 0) return -1;
    pata_select(d, (uint8_t)(lba >> 24));
    if (pata_wait_busy(d) < 0) return -1;   /* wait again after drive select */
    outb(g_ata_base[d] + 1, 0x00);
    outb(g_ata_base[d] + 2, 1);
    outb(g_ata_base[d] + 3, (uint8_t)(lba & 0xFF));
    outb(g_ata_base[d] + 4, (uint8_t)((lba >>  8) & 0xFF));
    outb(g_ata_base[d] + 5, (uint8_t)((lba >> 16) & 0xFF));
    outb(g_ata_base[d] + 7, ATA_CMD_READ);
    if (pata_wait_drq(d) < 0) return -1;
    uint16_t *w = (uint16_t *)buf;
    for (int i = 0; i < 256; i++) w[i] = inw(g_ata_base[d]);
    return 0;
}

/* PATA write — called from ata_write_sector when drive_type == ATA_TYPE_PATA */
static int pata_write(uint8_t d, uint32_t lba, const uint8_t *buf) {
    if (pata_wait_busy(d) < 0) return -1;
    pata_select(d, (uint8_t)(lba >> 24));
    if (pata_wait_busy(d) < 0) return -1;   /* wait again after drive select */
    outb(g_ata_base[d] + 1, 0x00);
    outb(g_ata_base[d] + 2, 1);
    outb(g_ata_base[d] + 3, (uint8_t)(lba & 0xFF));
    outb(g_ata_base[d] + 4, (uint8_t)((lba >>  8) & 0xFF));
    outb(g_ata_base[d] + 5, (uint8_t)((lba >> 16) & 0xFF));
    outb(g_ata_base[d] + 7, ATA_CMD_WRITE);
    if (pata_wait_drq(d) < 0) return -1;
    const uint16_t *w = (const uint16_t *)buf;
    for (int i = 0; i < 256; i++) outw(g_ata_base[d], w[i]);
    outb(g_ata_base[d] + 7, ATA_CMD_FLUSH);
    if (pata_wait_busy(d) < 0) return -1;
    return 0;
}

/* ═══════════════════════════════════════════════════════════════════════════
 * AHCI/SATA path
 * ═══════════════════════════════════════════════════════════════════════════
 *
 * The AHCI Host Bus Adapter (HBA) exposes all registers through a
 * memory-mapped region whose base address is in PCI BAR5 (ABAR).
 *
 * Layout at ABAR:
 *   0x000–0x0FF  Generic Host Control registers
 *   0x100+port*0x80  Per-port registers (32 ports max)
 *
 * For each active port we allocate three static buffers:
 *   CLB  Command List Buffer  (1024 bytes, 32 × 32-byte slots, 1 KB aligned)
 *   FB   FIS Receive Buffer   (256 bytes, 256-byte aligned)
 *   CT   Command Table        (256 bytes, 128-byte aligned)
 *        — holds H2D Register FIS (at CT+0x00) and one PRDT entry (at CT+0x80)
 *
 * We always use command slot 0, one sector at a time (no NCQ, no DMA SG).
 */

#define AHCI_MAX_PORTS  4   /* maximum AHCI drives tracked (indices 4-7) */

/* Per-port buffers: index 0 = drive slot 4, index 1 = drive slot 5, etc. */
static uint8_t ahci_clb[AHCI_MAX_PORTS][1024]
    __attribute__((aligned(1024)));
static uint8_t ahci_fb [AHCI_MAX_PORTS][256]
    __attribute__((aligned(256)));
static uint8_t ahci_ct [AHCI_MAX_PORTS][256]
    __attribute__((aligned(128)));

/* ── MMIO helpers ─────────────────────────────────────────────────────────── */

static inline uint32_t ahci_r(uint32_t base, uint32_t off) {
    return *(volatile uint32_t *)(uintptr_t)(base + off);
}
static inline void ahci_w(uint32_t base, uint32_t off, uint32_t val) {
    *(volatile uint32_t *)(uintptr_t)(base + off) = val;
}

/* ── HBA generic register offsets ────────────────────────────────────────── */
#define AHCI_GHC    0x04u   /* Global HBA Control                          */
#define AHCI_PI     0x0Cu   /* Ports Implemented bitmask                   */

#define AHCI_GHC_AE     (1u << 31)  /* AHCI Enable                        */
#define AHCI_GHC_RESET  (1u <<  0)  /* HBA Reset                          */

/* ── Per-port register offsets (relative to port base) ───────────────────── */
#define PxCLB   0x00u   /* Command List Base (low 32 bits)                 */
#define PxCLBU  0x04u   /* Command List Base (upper 32 bits, always 0)    */
#define PxFB    0x08u   /* FIS Base (low 32 bits)                          */
#define PxFBU   0x0Cu   /* FIS Base (upper 32 bits, always 0)             */
#define PxIS    0x10u   /* Interrupt Status  (write 1 to clear)            */
#define PxIE    0x14u   /* Interrupt Enable  (keep 0 — we poll)           */
#define PxCMD   0x18u   /* Command and Status                              */
#define PxTFD   0x20u   /* Task File Data (status[7:0] / error[15:8])     */
#define PxSSTS  0x28u   /* SATA Status                                     */
#define PxSERR  0x30u   /* SATA Error    (write 1 to clear)               */
#define PxCI    0x38u   /* Command Issue bitmask                           */

#define PXCMD_ST    (1u <<  0)  /* Start command list DMA engine           */
#define PXCMD_FRE   (1u <<  4)  /* FIS Receive Enable                     */
#define PXCMD_FR    (1u << 14)  /* FIS Receive Running (read-only)        */
#define PXCMD_CR    (1u << 15)  /* Command List Running (read-only)       */

/* PxSSTS DET field (bits 3:0) */
#define PXSSTS_DET_PRESENT  0x3u    /* device present + PHY comms OK      */
#define PXSSTS_DET_NOPHY    0x1u    /* device detected, no PHY comms yet  */

/* PxSCTL — SATA Control (used to issue COMRESET) */
#define PxSCTL  0x2Cu
#define PXSCTL_DET_COMRESET  0x1u   /* set DET field to 1 to assert COMRESET */

/* Compute port register base address */
static inline uint32_t ahci_port_base(uint32_t abar, uint8_t port) {
    return abar + 0x100u + (uint32_t)port * 0x80u;
}

/* ── Port engine control ──────────────────────────────────────────────────── */

/*
 * ahci_port_comreset — force a COMRESET on a port whose PHY is not yet
 * communicating (PxSSTS.DET == 1: device detected but no PHY comms).
 *
 * This is needed on some controllers where a port comes up in Slumber or
 * partial power state.  We write DET=1 to PxSCTL (assert COMRESET), hold
 * it long enough for the device to reset (~1 ms), then write DET=0 to
 * release and wait for DET=3 (comms established, ≤ ~150 ms).
 */
static void ahci_port_comreset(uint32_t pb) {
    uint32_t sctl = ahci_r(pb, PxSCTL);
    /* Set DET=1, preserve other fields */
    ahci_w(pb, PxSCTL, (sctl & ~0x0Fu) | PXSCTL_DET_COMRESET);
    /* Hold reset for ≥ 1 ms */
    for (uint32_t i = 0; i < 100000u; i++) __asm__ volatile ("nop");
    /* Release reset: DET=0 */
    ahci_w(pb, PxSCTL, sctl & ~0x0Fu);
    /* Wait for DET=3 (up to ~200 ms worth of iterations) */
    for (uint32_t i = 0; i < 1000000u; i++) {
        if ((ahci_r(pb, PxSSTS) & 0x0Fu) == PXSSTS_DET_PRESENT) break;
    }
    /* Clear errors that accumulated during reset */
    ahci_w(pb, PxSERR, 0xFFFFFFFFu);
}

/* Stop FIS reception and command list processing on a port. */
static void ahci_port_stop(uint32_t pb) {
    uint32_t cmd = ahci_r(pb, PxCMD);
    cmd &= ~(PXCMD_ST | PXCMD_FRE);
    ahci_w(pb, PxCMD, cmd);
    /* Wait for both CR and FR to deassert (up to ~1 ms) */
    for (uint32_t i = 0; i < 500000; i++) {
        if (!(ahci_r(pb, PxCMD) & (PXCMD_CR | PXCMD_FR))) break;
    }
}

/* Start FIS reception and command list processing on a port.
 * CLB and FB must already be set before calling. */
static void ahci_port_start(uint32_t pb) {
    /* Ensure CR is clear before setting ST (spec requirement) */
    for (uint32_t i = 0; i < 500000; i++)
        if (!(ahci_r(pb, PxCMD) & PXCMD_CR)) break;
    uint32_t cmd = ahci_r(pb, PxCMD);
    cmd |= PXCMD_FRE | PXCMD_ST;
    ahci_w(pb, PxCMD, cmd);
}

/* ── Command issuance ────────────────────────────────────────────────────── */

/*
 * ahci_issue — submit one ATA command to an AHCI port via command slot 0.
 *
 *   buf_idx  : index into ahci_clb / ahci_fb / ahci_ct (== drive_index - 4)
 *   pb       : port base MMIO address
 *   ata_cmd  : 0x20 = READ SECTORS, 0x30 = WRITE SECTORS, 0xEC = IDENTIFY
 *   lba      : LBA28 address (ignored for IDENTIFY)
 *   write    : 1 = write direction, 0 = read/identify
 *   buf      : 512-byte data buffer (physical addr == virtual in flat mode)
 *
 * Returns 0 on success, -1 on error.
 *
 * Memory layout of ahci_ct[buf_idx]:
 *   0x00–0x3F  CFIS — Command FIS (H2D Register FIS, 20 bytes used of 64)
 *   0x40–0x7F  ACMD / reserved   (not used for non-ATAPI)
 *   0x80–0x8F  PRDT entry 0      (one 16-byte Physical Region Descriptor)
 *
 * Command List Slot 0 layout (first 32 bytes of ahci_clb[buf_idx]):
 *   DW0  bits[4:0]=CFL=5, bit[6]=W, bits[31:16]=PRDTL=1
 *   DW1  PRDBC (written by HBA on completion)
 *   DW2  CTBA  (physical address of command table, low 32 bits)
 *   DW3  CTBAU (= 0, we are under 4 GB)
 *   DW4–DW7  reserved = 0
 */
static int ahci_issue(uint8_t buf_idx, uint32_t pb,
                      uint8_t ata_cmd, uint32_t lba,
                      int write, void *buf) {
    uint8_t *ct = ahci_ct[buf_idx];

    /* Zero the command table */
    for (int i = 0; i < 256; i++) ct[i] = 0;

    /* ── H2D Register FIS at CT+0x00 ────────────────────────────────────── */
    /*
     * H2D Register FIS (5 dwords = 20 bytes):
     *   Byte  0  FIS type = 0x27
     *   Byte  1  C=1 (command), PM port = 0  → 0x80
     *   Byte  2  ATA command
     *   Byte  3  Features (low) = 0
     *   Bytes 4–7  LBA[7:0], LBA[15:8], LBA[23:16], Device
     *   Bytes 8–11 LBA[31:24](ext=0 for LBA28), LBA[39:32]=0, LBA[47:40]=0, Features(hi)=0
     *   Byte 12  Sector count (low): 1 for R/W; 0 for IDENTIFY (unused)
     *   Bytes 13-19 Count(hi), ICC, Control, reserved
     *
     * Device byte (offset 7):
     *   bit7 = 1 (obsolete, set for compat)
     *   bit6 = 1 (LBA mode)
     *   bit5 = 1 (obsolete, set for compat)
     *   bit4 = 0 (device 0 / master)
     *   bits3:0 = LBA[27:24] for LBA28, or 0 for IDENTIFY
     */
    ct[0x00] = 0x27;                                    /* FIS type H2D     */
    ct[0x01] = 0x80;                                    /* C=1              */
    ct[0x02] = ata_cmd;
    ct[0x04] = (uint8_t)( lba        & 0xFF);           /* LBA[7:0]         */
    ct[0x05] = (uint8_t)((lba >>  8) & 0xFF);           /* LBA[15:8]        */
    ct[0x06] = (uint8_t)((lba >> 16) & 0xFF);           /* LBA[23:16]       */
    ct[0x07] = (ata_cmd == ATA_CMD_IDENTIFY)            /* Device register  */
               ? 0xE0u                                   /*   IDENTIFY: LBA mode, dev 0 */
               : (uint8_t)(0xE0u | ((lba >> 24) & 0x0Fu)); /* R/W: + LBA[27:24] */
    ct[0x08] = 0;                                       /* LBA ext = 0      */
    ct[0x0C] = (ata_cmd == ATA_CMD_IDENTIFY) ? 0 : 1;  /* Sector count     */

    /* ── PRDT entry at CT+0x80 ───────────────────────────────────────────── */
    /*
     * Physical Region Descriptor Table entry (16 bytes):
     *   DW0  DBA  = physical address of data buffer (low 32 bits)
     *   DW1  DBAU = 0 (high 32 bits — we are under 4 GB)
     *   DW2  Reserved = 0
     *   DW3  bits[21:0] = DBC (byte count - 1) = 511 = 0x1FF
     *        bit[31] = interrupt on completion = 0
     */
    uint32_t dba = (uint32_t)(uintptr_t)buf;
    ct[0x80] = (uint8_t)( dba        & 0xFF);
    ct[0x81] = (uint8_t)((dba >>  8) & 0xFF);
    ct[0x82] = (uint8_t)((dba >> 16) & 0xFF);
    ct[0x83] = (uint8_t)((dba >> 24) & 0xFF);
    /* DBAU (0x84-0x87) and reserved (0x88-0x8B) already zeroed */
    ct[0x8C] = 0xFF;    /* DBC[7:0]  = 511 & 0xFF */
    ct[0x8D] = 0x01;    /* DBC[15:8] = 511 >> 8   */
    /* ct[0x8E–0x8F] = 0 (DBC[21:16] = 0, interrupt = 0) */

    /* ── Command List Slot 0 ─────────────────────────────────────────────── */
    uint32_t *cl   = (uint32_t *)(uintptr_t)ahci_clb[buf_idx];
    uint32_t  ctba = (uint32_t)(uintptr_t)ct;
    cl[0] = (1u   << 16)                       /* PRDTL = 1                 */
          | ((uint32_t)(write ? 1u : 0u) << 6) /* W (write direction)       */
          | 5u;                                 /* CFL = 5 dwords (20 bytes) */
    cl[1] = 0u;                                /* PRDBC — filled by HBA     */
    cl[2] = ctba;                              /* CTBA (low 32 bits)        */
    cl[3] = 0u;                                /* CTBAU = 0                 */
    cl[4] = 0u; cl[5] = 0u; cl[6] = 0u; cl[7] = 0u; /* reserved           */

    /* ── Clear error / interrupt status ─────────────────────────────────── */
    ahci_w(pb, PxIS,   0xFFFFFFFFu);
    ahci_w(pb, PxSERR, 0xFFFFFFFFu);

    /* ── Issue command slot 0 ────────────────────────────────────────────── */
    ahci_w(pb, PxCI, 1u);

    /* ── Poll for completion (up to ~1 M iterations ≈ tens of ms) ────────── */
    for (uint32_t i = 0; i < 1000000u; i++) {
        if (!(ahci_r(pb, PxCI) & 1u)) break;
    }

    if (ahci_r(pb, PxCI) & 1u) return -1;          /* timed out            */
    uint32_t tfd = ahci_r(pb, PxTFD);
    if (tfd & 0x01u) return -1;                     /* ERR bit set          */
    if (tfd & 0x20u) return -1;                     /* DF  bit set          */
    return 0;
}

/* ── AHCI drive detection ─────────────────────────────────────────────────── */

/*
 * ahci_probe_hba — initialise one AHCI HBA already located on the PCI bus,
 * probe its ports, and fill g_drives starting at drive slot `slot`.
 *
 *   hba        : PCI device descriptor (already found by caller)
 *   slot       : first g_drives[] index to fill
 *   slots_left : how many more drive slots we can still fill
 *
 * Returns number of AHCI drives found on this HBA.
 */
static int ahci_probe_hba(pci_device_t *hba, int slot, int slots_left) {
    /*
     * BAR5 is the AHCI Base Address Register (ABAR).
     * MMIO BARs have bit 0 clear; strip the flags (bits 3:0).
     */
    uint32_t abar = hba->bar[5] & 0xFFFFFFF0u;
    if (abar == 0) return 0;    /* BAR not programmed */

    /*
     * Enable MMIO + bus-mastering on the HBA.
     * Without memory-space enable the CPU reads all-zeros from ABAR.
     */
    pci_enable_busmaster(hba);

    /* Ensure AHCI mode is active (set GHC.AE).  Some chipsets come up with
     * AHCI disabled; writing AE switches the HBA into AHCI register mode.  */
    uint32_t ghc = ahci_r(abar, AHCI_GHC);
    if (!(ghc & AHCI_GHC_AE)) {
        ahci_w(abar, AHCI_GHC, ghc | AHCI_GHC_AE);
        for (int i = 0; i < 10000; i++) __asm__ volatile ("nop");
    }

    uint32_t pi = ahci_r(abar, AHCI_PI);
    if (pi == 0) return 0;

    int found = 0;
    uint16_t id_buf[256];

    for (uint8_t port = 0; port < 32 && found < slots_left; port++) {
        if (!(pi & (1u << port))) continue;

        uint32_t pb   = ahci_port_base(abar, port);
        uint32_t ssts = ahci_r(pb, PxSSTS);
        uint8_t  det  = (uint8_t)(ssts & 0x0Fu);

        if (det == 0) continue;     /* no device at all */

        /*
         * DET == 1: device present but PHY not yet communicating.
         * This happens when a port wakes from Slumber or DevSleep.
         * Issue a COMRESET to force the PHY to re-negotiate.
         */
        if (det == PXSSTS_DET_NOPHY) {
            ahci_port_comreset(pb);
            det = (uint8_t)(ahci_r(pb, PxSSTS) & 0x0Fu);
        }
        if (det != PXSSTS_DET_PRESENT) continue;   /* still not up */

        uint8_t bidx = (uint8_t)(slot - 4 + found);  /* index into AHCI buffer pool */
        if (bidx >= AHCI_MAX_PORTS) break;

        /* ── Initialise port ───────────────────────────────────────────── */
        ahci_port_stop(pb);

        ahci_w(pb, PxCLB,  (uint32_t)(uintptr_t)ahci_clb[bidx]);
        ahci_w(pb, PxCLBU, 0u);
        ahci_w(pb, PxFB,   (uint32_t)(uintptr_t)ahci_fb[bidx]);
        ahci_w(pb, PxFBU,  0u);

        ahci_w(pb, PxIS,   0xFFFFFFFFu);
        ahci_w(pb, PxSERR, 0xFFFFFFFFu);
        ahci_w(pb, PxIE,   0u);

        ahci_port_start(pb);

        for (int i = 0; i < 50000; i++) __asm__ volatile ("nop");

        /* ── IDENTIFY ─────────────────────────────────────────────────── */
        if (ahci_issue(bidx, pb, ATA_CMD_IDENTIFY, 0, 0, id_buf) != 0)
            continue;

        /* ── Fill drive slot ──────────────────────────────────────────── */
        ata_drive_t *drv = &g_drives[slot + found];
        drv->present     = ATA_PRESENT;
        drv->drive_type  = ATA_TYPE_AHCI;
        drv->drive_index = (uint8_t)(slot + found);
        drv->ahci_abar   = abar;
        drv->ahci_port   = port;
        pata_parse_identify(id_buf, drv);

        found++;
    }

    return found;
}

/*
 * ahci_detect — locate all AHCI HBAs on the PCI bus and probe their ports.
 *
 * HBA search strategy (most specific first, broadening on failure):
 *   1. Class 01 / Sub 06 / Prog-IF 01  — standard AHCI 1.0
 *   2. Class 01 / Sub 06 / any prog_if — vendor-specific or pre-1.0 AHCI
 *      (some Intel ICH chips report prog_if=0x00 even when running AHCI)
 *
 * Within each strategy we walk all PCI functions via pci_find_class_after
 * so that machines with two AHCI controllers both get probed.
 */
static int ahci_detect(int first_free_slot) {
    int total = 0;
    int slot  = first_free_slot;
    pci_device_t hba;

    /* ── Pass 1: standard AHCI 1.0 (prog_if == 0x01) ───────────────────── */
    if (pci_find_device(0x01, 0x06, 0x01, &hba) == 0) {
        do {
            int slots_left = AHCI_MAX_PORTS - total;
            if (slots_left <= 0) break;
            int n = ahci_probe_hba(&hba, slot, slots_left);
            total += n;
            slot  += n;
        } while (pci_find_class_after(0x01, 0x06, &hba, &hba) == 0
                 && hba.prog_if == 0x01);
    }

    /* ── Pass 2: any prog_if — catches vendor-specific AHCI (prog_if=0x00) */
    /* Only run if pass 1 found nothing, or there are still free slots.      */
    if (total < AHCI_MAX_PORTS) {
        pci_device_t hba2;
        if (pci_find_class(0x01, 0x06, &hba2) == 0) {
            do {
                if (hba2.prog_if == 0x01) continue;   /* already handled   */
                int slots_left = AHCI_MAX_PORTS - total;
                if (slots_left <= 0) break;
                int n = ahci_probe_hba(&hba2, slot, slots_left);
                total += n;
                slot  += n;
            } while (pci_find_class_after(0x01, 0x06, &hba2, &hba2) == 0);
        }
    }

    return total;
}

/* ═══════════════════════════════════════════════════════════════════════════
 * Public API
 * ═══════════════════════════════════════════════════════════════════════════ */

int ata_detect(ata_drive_t out[ATA_MAX_DRIVES]) {
    /* Zero the entire drive table first */
    for (int i = 0; i < ATA_MAX_DRIVES; i++) {
        g_drives[i].present     = ATA_NOT_PRESENT;
        g_drives[i].drive_type  = ATA_TYPE_PATA;
        g_drives[i].drive_index = (uint8_t)i;
        g_drives[i].total_sectors = 0;
        g_drives[i].model[0]    = '\0';
        g_drives[i].ahci_abar   = 0;
        g_drives[i].ahci_port   = 0;
    }

    /* Probe PATA (fills slots 0-3) */
    int total = pata_detect();

    /* Probe AHCI (fills slots 4-7) */
    total += ahci_detect(4);

    /* Copy module table to caller's array */
    for (int i = 0; i < ATA_MAX_DRIVES; i++)
        out[i] = g_drives[i];

    return total;
}

int ata_read_sector(uint8_t drv, uint32_t lba, uint8_t *buf) {
    if (drv >= ATA_MAX_DRIVES)                return -1;
    if (!g_drives[drv].present)               return -1;

    if (g_drives[drv].drive_type == ATA_TYPE_AHCI) {
        uint8_t  bidx = drv - 4;
        uint32_t pb   = ahci_port_base(g_drives[drv].ahci_abar,
                                       g_drives[drv].ahci_port);
        return ahci_issue(bidx, pb, ATA_CMD_READ, lba, 0, buf);
    }

    return pata_read(drv, lba, buf);
}

int ata_write_sector(uint8_t drv, uint32_t lba, const uint8_t *buf) {
    if (drv >= ATA_MAX_DRIVES)                return -1;
    if (!g_drives[drv].present)               return -1;

    if (g_drives[drv].drive_type == ATA_TYPE_AHCI) {
        uint8_t  bidx = drv - 4;
        uint32_t pb   = ahci_port_base(g_drives[drv].ahci_abar,
                                       g_drives[drv].ahci_port);
        /* Cast away const — ahci_issue takes void*; we pass write=1 so
         * the HBA reads from the buffer, never writes to it.             */
        return ahci_issue(bidx, pb, ATA_CMD_WRITE, lba, 1, (void *)buf);
    }

    return pata_write(drv, lba, buf);
}
