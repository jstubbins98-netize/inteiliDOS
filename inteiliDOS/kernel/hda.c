/*
 * kernel/hda.c — Intel High Definition Audio (HDA) capture driver
 *
 * Implements minimal audio capture for KCS (Kansas City Standard) decoding
 * as used by InteiliBASIC CLOAD.
 *
 * ── HP Vectra VEi8 (1998) hardware note ────────────────────────────────────
 * The HP Vectra VEi8 does NOT have an Intel HDA controller.  HDA was
 * introduced in 2004 (ICH6).  The VEi8 uses AC'97 audio — typically a
 * Crystal Semiconductor CS4281 or compatible PCI audio codec, or a
 * SoundBlaster-compatible ISA/PCI card.
 *
 * This driver will NOT find a device on a real VEi8: hda_init() will fail
 * the PCI scan (class 0x04 / subclass 0x03) and hda_has_signal() will
 * always return 0.  CLOAD will display "No signal detected" as expected.
 *
 * To enable cassette audio capture on the VEi8, a separate AC'97 / CS4281
 * driver (kernel/ac97.c) would need to be written in its place.
 * ────────────────────────────────────────────────────────────────────────────
 *
 * Design constraints (bare-metal inteilidOS):
 *   • No stdlib, no libc — k-prefixed helpers only
 *   • No 64-bit arithmetic
 *   • No floating point
 *   • Static buffers only (DMA memory must be physically contiguous and
 *     aligned — static BSS satisfies this in a flat ring-0 binary)
 *   • Must compile clean under both -march=pentium3 and -march=pentium2
 *
 * HDA register map (MMIO, 32-bit base from BAR0):
 *   0x00  GCAP     (2) Global Capabilities; bits[11:8] = ISS (input streams)
 *   0x08  GCTL     (4) Global Control  (bit 0 = CRST)
 *   0x0E  STATESTS (2) Codec State Change Status (bit N = codec N present)
 *   0x40  CORBLBASE(4) CORB Lower Base Address
 *   0x44  CORBUBASE(4) CORB Upper Base Address (always 0 — 32-bit PA)
 *   0x48  CORBWP   (2) CORB Write Pointer
 *   0x4A  CORBRP   (2) CORB Read  Pointer (bit 15 = reset)
 *   0x4C  CORBCTL  (1) CORB Control (bit 1 = CORBRUN)
 *   0x4D  CORBSTS  (1) CORB Status
 *   0x4E  CORBSIZE (1) CORB Size (0x02 = 256 entries)
 *   0x50  RIRBLBASE(4) RIRB Lower Base Address
 *   0x54  RIRIBUBASE(4) RIRB Upper Base Address
 *   0x58  RIRBWP   (2) RIRB Write Pointer (bit 15 = reset)
 *   0x5A  RINTCNT  (2) Response Interrupt Count
 *   0x5C  RIRBCTL  (1) RIRB Control (bit 1 = RIRBDMAEN)
 *   0x5D  RIRBSTS  (1) RIRB Status
 *   0x5E  RIRBSIZE (1) RIRB Size (0x02 = 256 entries)
 *
 * Input Stream Descriptor 0 at MMIO offset 0x80
 * (output streams come first; input streams follow at 0x80 + OSS*0x20,
 *  but we always use the first stream descriptor regardless — on real HDA
 *  controllers the first input stream is at 0x80 + OSS*0x20; for simplicity
 *  we probe GCAP.ISS and offset appropriately):
 *   +0x00  CTL[2:0]  (3 bytes)  Stream Control
 *   +0x03  STS       (1 byte)   Stream Status
 *   +0x04  LPIB      (4 bytes)  Link Position In Buffer
 *   +0x08  CBL       (4 bytes)  Cyclic Buffer Length
 *   +0x0C  LVI       (2 bytes)  Last Valid Index
 *   +0x12  FMT       (2 bytes)  Stream Format
 *   +0x18  BDPL      (4 bytes)  BDL Lower Base
 *   +0x1C  BDPU      (4 bytes)  BDL Upper Base
 *
 * Stream Format for 8 kHz / 8-bit / mono:
 *   Base=48 kHz (bit14=0), divisor field=5 (÷6 → 8000 Hz), 8-bit, 1 ch
 *   → 0x0500
 *
 * KCS decoding at 8000 Hz:
 *   Samples per bit period = 8000/1200 ≈ 7
 *   0-bit (1200 Hz): ~2 zero-crossings in 7 samples
 *   1-bit (2400 Hz): ~4 zero-crossings in 7 samples
 *   Decision threshold: ≥ 3 crossings → 1-bit, < 3 → 0-bit
 */

#include "hda.h"
#include "pci.h"
#include "timer.h"
#include "memory.h"
#include <stdint.h>

/* =========================================================================
 * MMIO helpers — volatile pointer access into the HDA register space
 * ========================================================================= */
static inline uint8_t  hda_rd8 (uint32_t base, uint32_t off) {
    return *(volatile uint8_t  *)(uintptr_t)(base + off);
}
static inline uint16_t hda_rd16(uint32_t base, uint32_t off) {
    return *(volatile uint16_t *)(uintptr_t)(base + off);
}
static inline uint32_t hda_rd32(uint32_t base, uint32_t off) {
    return *(volatile uint32_t *)(uintptr_t)(base + off);
}
static inline void hda_wr8 (uint32_t base, uint32_t off, uint8_t  v) {
    *(volatile uint8_t  *)(uintptr_t)(base + off) = v;
}
static inline void hda_wr16(uint32_t base, uint32_t off, uint16_t v) {
    *(volatile uint16_t *)(uintptr_t)(base + off) = v;
}
static inline void hda_wr32(uint32_t base, uint32_t off, uint32_t v) {
    *(volatile uint32_t *)(uintptr_t)(base + off) = v;
}

/* =========================================================================
 * HDA global register offsets
 * ========================================================================= */
#define HDA_GCAP        0x00u
#define HDA_GCTL        0x08u
#define HDA_STATESTS    0x0Eu
#define HDA_CORBLBASE   0x40u
#define HDA_CORBUBASE   0x44u
#define HDA_CORBWP      0x48u
#define HDA_CORBRP      0x4Au
#define HDA_CORBCTL     0x4Cu
#define HDA_CORBSTS     0x4Du
#define HDA_CORBSIZE    0x4Eu
#define HDA_RIRBLBASE   0x50u
#define HDA_RIRIBUBASE  0x54u
#define HDA_RIRBWP      0x58u
#define HDA_RINTCNT     0x5Au
#define HDA_RIRBCTL     0x5Cu
#define HDA_RIRBSTS     0x5Du
#define HDA_RIRBSIZE    0x5Eu

/* Stream descriptor register offsets (relative to stream descriptor base) */
#define HDA_SD_CTL      0x00u   /* 3 bytes control + 1 byte status         */
#define HDA_SD_STS      0x03u
#define HDA_SD_LPIB     0x04u
#define HDA_SD_CBL      0x08u
#define HDA_SD_LVI      0x0Cu
#define HDA_SD_FMT      0x12u
#define HDA_SD_BDPL     0x18u
#define HDA_SD_BDPU     0x1Cu

/* Each stream descriptor is 0x20 bytes wide in the MMIO map */
#define HDA_SD_SIZE     0x20u

/* Stream format: 8 kHz, 8-bit, mono
 * Base=48 kHz (bit14=0), divisor=5 (bits[10:8]=5 → ÷6 → 8000 Hz),
 * 8-bit (bits[5:4]=0), 1 channel (bits[3:0]=0)                           */
#define HDA_FMT_8KHZ_8BIT_MONO  0x0500u

/* HDA verb GET_PARAMETER sub-parameter IDs */
#define HDA_PARAM_SUBNODE    0x04u   /* Subordinate Node Count             */
#define HDA_PARAM_FN_TYPE    0x05u   /* Function Group Type (0x01 = Audio) */
#define HDA_PARAM_WIDGET_CAP 0x09u   /* Audio Widget Capabilities          */

/* Widget types extracted from bits[23:20] of Widget Capabilities */
#define HDA_WTYPE_ADC        0x01u   /* Audio Input (ADC)                  */
#define HDA_WTYPE_PIN        0x04u   /* Pin Complex (input/output pin)     */

/* Verb codes (12-bit field in CORB entry) */
#define HDA_VERB_GET_PARAM   0xF00u  /* GET_PARAMETER                      */
#define HDA_VERB_SET_CONN    0x701u  /* Set Connection Select Control      */
#define HDA_VERB_SET_POWER   0x705u  /* Set Power State                    */
#define HDA_VERB_SET_STREAM  0x706u  /* Set Stream/Channel                 */
#define HDA_VERB_SET_PIN     0x707u  /* Set Pin Widget Control             */
#define HDA_VERB_SET_FMT     0x200u  /* Set Stream Format (16-bit payload) */

/* =========================================================================
 * Static DMA buffers
 *
 * BDL entry: lower-32-bit address | upper-32-bit address (=0) | length | IOC
 * We use 2 BDL entries (ping-pong) each pointing to half of g_capture_buf.
 *
 * Alignment requirements (HDA spec):
 *   BDL base  : ≥ 128-byte aligned
 *   CORB base : ≥ 128-byte aligned
 *   RIRB base : ≥ 128-byte aligned
 *   Capture buffer : no spec alignment requirement beyond word-size, but
 *                    128-byte alignment keeps us safe.
 * ========================================================================= */

#define HDA_CAP_BUF_HALF   2048u                        /* bytes per BDL entry   */
#define HDA_CAP_BUF_TOTAL  (HDA_CAP_BUF_HALF * 2u)     /* 4 KB ping-pong ring   */
#define HDA_BDL_ENTRIES    2u

/* BDL entry layout — §3.6.2 of Intel HDA spec */
typedef struct {
    uint32_t addr_lo;   /* physical address bits[31:0]  */
    uint32_t addr_hi;   /* physical address bits[63:32] (always 0)          */
    uint32_t length;    /* segment byte count                                */
    uint32_t ioc;       /* bit 0 = Interrupt On Completion                  */
} __attribute__((packed)) hda_bdl_entry_t;

/* RIRB entry — 64-bit response: 32-bit verb response + 32-bit extended */
typedef struct {
    uint32_t resp;
    uint32_t resp_ex;
} __attribute__((packed)) hda_rirb_entry_t;

static hda_bdl_entry_t  g_bdl[HDA_BDL_ENTRIES]
    __attribute__((aligned(128)));

static uint32_t         g_corb[256]
    __attribute__((aligned(128)));

static hda_rirb_entry_t g_rirb[256]
    __attribute__((aligned(128)));

static uint8_t          g_capture_buf[HDA_CAP_BUF_TOTAL]
    __attribute__((aligned(128)));

/* =========================================================================
 * Driver state
 * ========================================================================= */
static int      g_hda_present  = 0;  /* 1 after successful hda_init()      */
static uint32_t g_hda_mmio     = 0;  /* MMIO base (from PCI BAR0)          */
static uint32_t g_sd_base      = 0;  /* stream descriptor MMIO base        */
static uint32_t g_read_pos     = 0;  /* consumer offset in capture ring    */
static uint8_t  g_corb_wp      = 0;  /* CORB write pointer (local shadow)  */
static uint8_t  g_rirb_rp      = 0;  /* RIRB read  pointer (local shadow)  */

/* =========================================================================
 * CORB / RIRB codec verb interface
 *
 * A 32-bit CORB entry encodes:
 *   [31:28] codec address (0–14)
 *   [27:20] NID (node ID)
 *   [19:8]  12-bit verb code
 *   [7:0]   8-bit payload
 * ========================================================================= */
static uint32_t hda_send_verb(uint8_t codec_addr, uint8_t nid,
                               uint32_t verb12, uint8_t payload)
{
    uint8_t  wp_new, rirb_wp;
    uint32_t cmd, timeout;

    cmd = ((uint32_t)(codec_addr & 0x0Fu) << 28)
        | ((uint32_t)nid           << 20)
        | ((verb12 & 0xFFFu)       <<  8)
        | (uint32_t)payload;

    wp_new          = (uint8_t)(g_corb_wp + 1u);
    g_corb[wp_new]  = cmd;
    hda_wr16(g_hda_mmio, HDA_CORBWP, (uint16_t)wp_new);
    g_corb_wp       = wp_new;

    /* Poll RIRBWP until it advances */
    timeout = 100000u;
    while (timeout--) {
        rirb_wp = (uint8_t)(hda_rd16(g_hda_mmio, HDA_RIRBWP) & 0xFFu);
        if (rirb_wp != g_rirb_rp) {
            g_rirb_rp = rirb_wp;
            hda_wr8(g_hda_mmio, HDA_RIRBSTS, 0x05u); /* clear RIRBOIS/RINTFL */
            return g_rirb[rirb_wp].resp;
        }
        __asm__ volatile ("nop");
    }
    return 0u;   /* timeout — codec did not respond */
}

/* =========================================================================
 * hda_init — locate, reset, and arm the HDA controller
 * ========================================================================= */
int hda_init(void)
{
    pci_device_t pdev;
    uint32_t     bar0, gcap;
    uint16_t     statests;
    uint32_t     timeout;
    uint8_t      oss;          /* output stream count  */
    int          codec;
    uint32_t     pa_corb, pa_rirb, pa_bdl, pa_buf;

    if (g_hda_present) return 1;

    /* ── 1. Find HDA controller (PCI class 0x04, subclass 0x03) ──────── */
    if (pci_find_device(0x04u, 0x03u, 0x00u, &pdev) != 0) {
        /* Not found; CLOAD will show "no signal" gracefully */
        return 0;
    }

    /* ── 2. Enable bus-master DMA ────────────────────────────────────── */
    pci_enable_busmaster(&pdev);

    /* ── 3. MMIO base from BAR0 (memory BAR, mask type bits[3:0]) ─────── */
    bar0 = pdev.bar[0] & 0xFFFFFFF0u;
    if (bar0 == 0u) return 0;
    g_hda_mmio = bar0;

    /* ── 4. Controller reset: assert CRST=0, wait, deassert CRST=1 ───── */
    hda_wr32(g_hda_mmio, HDA_GCTL, 0u);
    timeout = 100000u;
    while (timeout-- && (hda_rd32(g_hda_mmio, HDA_GCTL) & 1u)) {
        __asm__ volatile ("nop");
    }
    timer_sleep(1u);   /* ≥ 100 µs in reset (HDA spec §5.5) */

    hda_wr32(g_hda_mmio, HDA_GCTL, 1u);
    timeout = 100000u;
    while (timeout-- && !(hda_rd32(g_hda_mmio, HDA_GCTL) & 1u)) {
        __asm__ volatile ("nop");
    }
    timer_sleep(2u);   /* codec cold-reset stabilisation */

    /* ── 5. Wait for at least one codec to set a STATESTS bit ────────── */
    statests = 0u;
    timeout  = 100u;   /* up to 100 ms */
    while (timeout--) {
        statests = hda_rd16(g_hda_mmio, HDA_STATESTS);
        if (statests) break;
        timer_sleep(1u);
    }
    if (!statests) {
        g_hda_mmio = 0u;
        return 0;
    }

    /* Find lowest-numbered codec (first set bit) */
    codec = 0;
    while (codec < 15 && !((statests >> codec) & 1u)) codec++;

    /* Clear STATESTS (W1C) */
    hda_wr16(g_hda_mmio, HDA_STATESTS, statests);

    /* ── 6. Determine stream descriptor base from GCAP ───────────────── */
    /* GCAP bits[15:12] = ISS (Input Stream Supported)
     *       bits[11:8]  = OSS (Output Stream Supported)
     * Stream descriptors start at MMIO+0x80: OSS output descriptors first,
     * then ISS input descriptors.  First input stream = 0x80 + OSS*0x20.  */
    gcap = (uint32_t)hda_rd16(g_hda_mmio, HDA_GCAP);
    oss  = (uint8_t)((gcap >> 8u) & 0x0Fu);   /* OSS = bits[11:8]         */
    g_sd_base = g_hda_mmio + 0x80u + (uint32_t)oss * HDA_SD_SIZE;

    /* ── 7. Set up CORB ──────────────────────────────────────────────── */
    hda_wr8(g_hda_mmio, HDA_CORBCTL, 0u);   /* stop DMA */
    timer_sleep(1u);

    pa_corb = (uint32_t)(uintptr_t)g_corb;
    hda_wr32(g_hda_mmio, HDA_CORBLBASE, pa_corb);
    hda_wr32(g_hda_mmio, HDA_CORBUBASE, 0u);
    hda_wr8 (g_hda_mmio, HDA_CORBSIZE,  0x02u);  /* 256 entries */

    /* Reset CORB read pointer (set bit 15, then clear) */
    hda_wr16(g_hda_mmio, HDA_CORBRP, 0x8000u);
    timeout = 10000u;
    while (timeout-- && !(hda_rd16(g_hda_mmio, HDA_CORBRP) & 0x8000u)) {
        __asm__ volatile ("nop");
    }
    hda_wr16(g_hda_mmio, HDA_CORBRP, 0x0000u);
    timeout = 10000u;
    while (timeout-- && (hda_rd16(g_hda_mmio, HDA_CORBRP) & 0x8000u)) {
        __asm__ volatile ("nop");
    }

    hda_wr16(g_hda_mmio, HDA_CORBWP, 0u);
    g_corb_wp = 0u;

    hda_wr8(g_hda_mmio, HDA_CORBCTL, 0x02u);   /* CORBRUN = 1 */

    /* ── 8. Set up RIRB ──────────────────────────────────────────────── */
    hda_wr8(g_hda_mmio, HDA_RIRBCTL, 0u);   /* stop DMA */
    timer_sleep(1u);

    pa_rirb = (uint32_t)(uintptr_t)g_rirb;
    hda_wr32(g_hda_mmio, HDA_RIRBLBASE, pa_rirb);
    hda_wr32(g_hda_mmio, HDA_RIRIBUBASE, 0u);
    hda_wr8 (g_hda_mmio, HDA_RIRBSIZE,   0x02u);  /* 256 entries */

    hda_wr16(g_hda_mmio, HDA_RIRBWP, 0x8000u);   /* reset write pointer */
    g_rirb_rp = 0u;

    hda_wr16(g_hda_mmio, HDA_RINTCNT, 1u);       /* respond after 1 entry */
    hda_wr8 (g_hda_mmio, HDA_RIRBCTL, 0x02u);    /* RIRBDMAEN = 1 */

    /* ── 9. Enumerate codec and configure capture path ───────────────── */
    {
        uint32_t resp;
        uint8_t  afg_nid = 0u, adc_nid = 0u, pin_nid = 0u;
        uint8_t  start, count, i;

        /* Root node subordinate count → locate Audio Function Group */
        resp  = hda_send_verb((uint8_t)codec, 0u,
                              HDA_VERB_GET_PARAM, HDA_PARAM_SUBNODE);
        start = (uint8_t)(resp >> 16);
        count = (uint8_t)(resp & 0xFFu);

        for (i = 0u; i < count && afg_nid == 0u; i++) {
            uint8_t  nid  = (uint8_t)(start + i);
            uint8_t  ftype;
            ftype = (uint8_t)(hda_send_verb((uint8_t)codec, nid,
                                            HDA_VERB_GET_PARAM,
                                            HDA_PARAM_FN_TYPE) & 0xFFu);
            if (ftype == 0x01u) afg_nid = nid;
        }

        if (afg_nid) {
            /* Power on the Audio Function Group */
            hda_send_verb((uint8_t)codec, afg_nid, HDA_VERB_SET_POWER, 0x00u);
            timer_sleep(10u);

            /* Enumerate widgets */
            resp  = hda_send_verb((uint8_t)codec, afg_nid,
                                  HDA_VERB_GET_PARAM, HDA_PARAM_SUBNODE);
            start = (uint8_t)(resp >> 16);
            count = (uint8_t)(resp & 0xFFu);

            for (i = 0u; i < count; i++) {
                uint8_t  nid   = (uint8_t)(start + i);
                uint32_t wcap  = hda_send_verb((uint8_t)codec, nid,
                                               HDA_VERB_GET_PARAM,
                                               HDA_PARAM_WIDGET_CAP);
                uint8_t  wtype = (uint8_t)((wcap >> 20u) & 0x0Fu);

                if (wtype == HDA_WTYPE_ADC && adc_nid == 0u)
                    adc_nid = nid;
                else if (wtype == HDA_WTYPE_PIN && pin_nid == 0u)
                    pin_nid = nid;
            }
        }

        /* Configure input pin: enable input (bit 5 of Pin Widget Control) */
        if (pin_nid) {
            hda_send_verb((uint8_t)codec, pin_nid, HDA_VERB_SET_PIN,  0x20u);
        }

        /* Configure ADC: power on, connect to first source, set format,
         * assign stream tag 1 / channel 0                                */
        if (adc_nid) {
            hda_send_verb((uint8_t)codec, adc_nid, HDA_VERB_SET_POWER,  0x00u);
            hda_send_verb((uint8_t)codec, adc_nid, HDA_VERB_SET_CONN,   0x00u);
            /* Stream format is a 16-bit value; HDA_VERB_SET_FMT (0x200)
             * encodes the upper 4 bits of the format in the verb index and
             * the lower 8 bits in the payload.
             * 0x0500: upper nibble = 0x0, lower byte = 0x00; index = 0x05 */
            hda_send_verb((uint8_t)codec, adc_nid,
                          HDA_VERB_SET_FMT | (uint32_t)((HDA_FMT_8KHZ_8BIT_MONO >> 8u) & 0x0Fu),
                          (uint8_t)(HDA_FMT_8KHZ_8BIT_MONO & 0xFFu));
            /* Stream tag 1, channel 0 → value = 0x10 */
            hda_send_verb((uint8_t)codec, adc_nid, HDA_VERB_SET_STREAM, 0x10u);
        }
    }

    /* ── 10. Build BDL (ping-pong: two halves of g_capture_buf) ─────── */
    kmemset(g_bdl, 0, sizeof(g_bdl));
    /* Pre-fill capture buffer with silence (0x80 = midpoint for 8-bit PCM) */
    kmemset(g_capture_buf, 0x80u, sizeof(g_capture_buf));

    pa_buf = (uint32_t)(uintptr_t)g_capture_buf;

    g_bdl[0].addr_lo = pa_buf;
    g_bdl[0].addr_hi = 0u;
    g_bdl[0].length  = HDA_CAP_BUF_HALF;
    g_bdl[0].ioc     = 1u;

    g_bdl[1].addr_lo = pa_buf + HDA_CAP_BUF_HALF;
    g_bdl[1].addr_hi = 0u;
    g_bdl[1].length  = HDA_CAP_BUF_HALF;
    g_bdl[1].ioc     = 1u;

    /* ── 11. Programme stream descriptor (first input stream) ────────── */
    /* Stop and reset the stream */
    hda_wr8(g_sd_base, HDA_SD_CTL, 0u);
    timer_sleep(2u);

    hda_wr8(g_sd_base, HDA_SD_CTL, 0x01u);   /* SRST = 1 */
    timeout = 10000u;
    while (timeout-- && !(hda_rd8(g_sd_base, HDA_SD_CTL) & 0x01u)) {
        __asm__ volatile ("nop");
    }
    hda_wr8(g_sd_base, HDA_SD_CTL, 0x00u);   /* SRST = 0 */
    timeout = 10000u;
    while (timeout-- && (hda_rd8(g_sd_base, HDA_SD_CTL) & 0x01u)) {
        __asm__ volatile ("nop");
    }

    hda_wr32(g_sd_base, HDA_SD_CBL,  HDA_CAP_BUF_TOTAL);
    hda_wr16(g_sd_base, HDA_SD_LVI,  (uint16_t)(HDA_BDL_ENTRIES - 1u));
    hda_wr16(g_sd_base, HDA_SD_FMT,  HDA_FMT_8KHZ_8BIT_MONO);

    pa_bdl = (uint32_t)(uintptr_t)g_bdl;
    hda_wr32(g_sd_base, HDA_SD_BDPL, pa_bdl);
    hda_wr32(g_sd_base, HDA_SD_BDPU, 0u);

    /* Set stream number (tag 1) in CTL bits[23:20].
     * Read the current 32-bit CTL word, update the stream number field. */
    {
        uint32_t ctl32 = hda_rd32(g_sd_base, HDA_SD_CTL);
        ctl32 &= ~0x00F00000u;     /* clear stream number  */
        ctl32 |=  0x00100000u;     /* stream tag = 1       */
        hda_wr32(g_sd_base, HDA_SD_CTL, ctl32);
    }

    /* ── 12. Start the stream (set RUN bit = bit 1 of CTL byte 0) ────── */
    {
        uint8_t ctl0 = hda_rd8(g_sd_base, HDA_SD_CTL);
        hda_wr8(g_sd_base, HDA_SD_CTL, (uint8_t)(ctl0 | 0x02u));
    }

    g_read_pos    = 0u;
    g_hda_present = 1;
    return 1;
}

/* =========================================================================
 * hda_has_signal — detect audio activity above the noise floor
 *
 * Reads up to 512 samples from the portion of the ring the DMA engine has
 * most recently written, and checks whether any sample deviates more than
 * a threshold from the 0x80 silence midpoint (8-bit unsigned PCM).
 * ========================================================================= */
int hda_has_signal(void)
{
    uint32_t lpib, check_start, i;

    if (!g_hda_present) {
        if (!hda_init()) return 0;
    }

    lpib = hda_rd32(g_sd_base, HDA_SD_LPIB);
    if (lpib >= HDA_CAP_BUF_TOTAL) lpib = 0u;

    /* Examine the 512 bytes immediately behind the DMA write head */
    check_start = (lpib >= 512u) ? (lpib - 512u) : 0u;

    for (i = check_start; i < check_start + 512u && i < HDA_CAP_BUF_TOTAL; i++) {
        uint8_t  s   = g_capture_buf[i];
        int32_t  dev = (int32_t)(uint32_t)s - 0x80;
        if (dev < 0) dev = -dev;
        if (dev > 20) return 1;   /* ±20/128 ≈ 15 % noise-floor threshold */
    }
    return 0;
}

/* =========================================================================
 * Internal helpers for KCS decoding
 * ========================================================================= */

/* Wait until the DMA engine has written at least 'n' more bytes beyond
 * g_read_pos into the ring.  Returns 1 on success, 0 on timeout.          */
static int hda_wait_samples(uint32_t n, uint32_t timeout_ms)
{
    uint32_t start = timer_get_ticks();

    while ((timer_get_ticks() - start) < timeout_ms) {
        uint32_t lpib = hda_rd32(g_sd_base, HDA_SD_LPIB);
        uint32_t avail;

        if (lpib >= HDA_CAP_BUF_TOTAL) lpib = 0u;

        avail = (lpib >= g_read_pos)
                ? (lpib - g_read_pos)
                : (HDA_CAP_BUF_TOTAL - g_read_pos + lpib);

        if (avail >= n) return 1;
        __asm__ volatile ("nop");
    }
    return 0;
}

/* Consume one sample from the ring and advance g_read_pos. */
static uint8_t hda_next_sample(void)
{
    uint8_t s = g_capture_buf[g_read_pos];
    g_read_pos = (g_read_pos + 1u) % HDA_CAP_BUF_TOTAL;
    return s;
}

/* Count 0x80-midpoint crossings in the next 'count' samples, given that
 * the sample immediately before this window was 'prev_s'.
 * Updates *prev_out to the last sample consumed.                           */
static int hda_count_crossings(uint8_t prev_s, uint32_t count,
                               uint8_t *prev_out)
{
    int     crossings = 0;
    uint8_t last = prev_s;
    uint32_t i;

    for (i = 0u; i < count; i++) {
        uint8_t s = hda_next_sample();
        if (((last <  0x80u) && (s >= 0x80u)) ||
            ((last >= 0x80u) && (s <  0x80u))) {
            crossings++;
        }
        last = s;
    }
    if (prev_out) *prev_out = last;
    return crossings;
}

/* Decode one bit from the ring: consume 7 samples, count crossings.
 * ≥ 3 crossings → 1-bit (2400 Hz); < 3 → 0-bit (1200 Hz).
 * Returns 0 or 1 on success, -1 on timeout.                               */
static int hda_decode_bit(uint8_t *prev_s)
{
    int     xings;
    uint8_t last;

    if (!hda_wait_samples(7u, 20u)) return -1;

    xings = hda_count_crossings(*prev_s, 7u, &last);
    *prev_s = last;

    return (xings >= 3) ? 1 : 0;
}

/* =========================================================================
 * hda_capture_kcs_byte — decode one KCS-framed byte (8N1, LSB first)
 *
 * Scans the capture ring for a start bit (0-bit after silence/leader),
 * then reads 8 data bits and one stop bit.
 *
 * Return values:
 *   0–255  decoded byte (success)
 *   -1     framing error: stop bit was not a 1-bit (data corruption)
 *   -2     timeout: no start bit or data bit arrived within the window
 *          (normal end-of-stream or audio dropout)
 * ========================================================================= */
int hda_capture_kcs_byte(void)
{
    uint8_t  prev_s  = 0x80u;
    uint8_t  byte_val;
    int      bit, i;

    if (!g_hda_present) {
        if (!hda_init()) return -2;   /* no hardware — treat as timeout */
    }

    uint32_t byte_start = timer_get_ticks();  /* 2-second window per byte */

    /* ── Wait for start bit (0-bit = 1200 Hz after idle or leader) ───── */
    while ((timer_get_ticks() - byte_start) < 2000u) {
        uint8_t s;
        int32_t dev;
        int     xings;
        uint8_t after;

        if (!hda_wait_samples(1u, 50u)) continue;
        s = hda_next_sample();

        dev = (int32_t)(uint32_t)s - 0x80;
        if (dev < 0) dev = -dev;
        if (dev < 10) {
            /* Input is still quiet; absorb sample and keep looking */
            prev_s = s;
            continue;
        }

        /* Signal is active.  Count crossings in the next 6 samples
         * (together with 's' that's 7 samples total for one bit period). */
        if (!hda_wait_samples(6u, 20u)) { prev_s = s; continue; }

        xings = hda_count_crossings(s, 6u, &after);
        prev_s = after;

        if (xings <= 2) {
            /* Matches 0-bit profile (1200 Hz) — treat this as start bit */
            break;
        }
        /* Otherwise it's a 1-bit (leader / inter-frame 2400 Hz); continue */
    }

    if ((timer_get_ticks() - byte_start) >= 2000u) return -2;  /* timeout */

    /* ── Read 8 data bits (D0 first, LSB first) ──────────────────────── */
    byte_val = 0u;
    for (i = 0; i < 8; i++) {
        bit = hda_decode_bit(&prev_s);
        if (bit < 0) return -2;   /* bit-period timeout — audio dropout */
        if (bit) byte_val |= (uint8_t)(1u << i);
    }

    /* ── Verify stop bit (must be a 1-bit = 2400 Hz) ─────────────────── */
    bit = hda_decode_bit(&prev_s);
    if (bit < 0) return -2;   /* timeout waiting for stop bit */
    if (bit != 1) return -1;  /* stop bit wrong — framing error */

    return (int)(uint32_t)byte_val;
}
