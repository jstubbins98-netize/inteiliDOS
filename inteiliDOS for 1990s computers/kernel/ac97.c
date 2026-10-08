/*
 * kernel/ac97.c — AC'97 audio capture driver (Crystal CS4281 / generic AC'97)
 *
 * Implements minimal audio capture for KCS (Kansas City Standard) decoding
 * as used by InteiliBASIC CLOAD.
 *
 * ── HP Vectra VEi8 (1998) hardware note ────────────────────────────────────
 * The HP Vectra VEi8 uses AC'97 audio, typically a Crystal Semiconductor
 * CS4281 PCI audio controller.  The CS4281 is a single-chip solution that
 * integrates the AC'97 controller and codec; it uses memory-mapped I/O via
 * BAR0 (BA0) and BAR1 (BA1 — sample RAM for FIFO).
 *
 * The Intel HDA driver (kernel/hda.c) will NOT find a device on the VEi8
 * because HDA was not introduced until 2004 (ICH6).
 * ────────────────────────────────────────────────────────────────────────────
 *
 * CS4281 PCI identification:
 *   Vendor 0x1013 / Device 0x4281
 *   (also detected via class 0x04 / subclass 0x01 for generic AC'97 audio)
 *
 * CS4281 register space (BA0, MMIO via BAR0):
 *   Capture uses DMA Channel B (channel 1):
 *     BA0_DBA1  (0x0048) — Capture DMA Base Address
 *     BA0_DBC1  (0x004C) — Capture DMA Byte Count (buffer size – 1)
 *     BA0_DBI1  (0x0058) — Capture DMA Buffer Index (current write position)
 *   FIFO (channel 1, capture):
 *     BA0_FCR1  (0x010C) — FIFO Control Register 1
 *     BA0_FPDR1 (0x0114) — FIFO Port Data Register 1
 *   Clock / AC-Link:
 *     BA0_CLKCR1 (0x0400) — Clock Control Register 1
 *     BA0_SERMC1 (0x0420) — Serial Port Master Control Register 1
 *   Codec access (AC-Link):
 *     BA0_ACCAD (0x046C) — AC Codec Address (write codec register address)
 *     BA0_ACCDA (0x0470) — AC Codec Data    (write codec register value)
 *     BA0_ACISV (0x0474) — AC Input Slot Valid
 *     BA0_ACSAD (0x0478) — AC Status Address (read codec register address)
 *     BA0_ACSDA (0x047C) — AC Status Data    (read codec register value)
 *   DMA control:
 *     BA0_DMR1  (0x0064) — DMA Mode Register 1
 *     BA0_DCR1  (0x006C) — DMA Command Register 1
 *     BA0_SSCR  (0x0800) — Sound System Control Register
 *     BA0_SRCSA (0x0804) — SRC Slot Assignment Register
 *     BA0_HICR  (0x0004) — Host Interrupt Control Register
 *     BA0_HIMR  (0x0008) — Host Interrupt Mask Register
 *
 * AC'97 codec register addresses (accessed through ACCAD/ACCDA):
 *   0x00 — Reset
 *   0x1A — Record Select
 *   0x1C — Record Gain (ADC capture gain)
 *   0x2A — Extended Audio ID (bit 0 = VRA: variable rate audio)
 *   0x2C — Extended Audio Status/Control (bit 0 = VRA enable)
 *   0x38 — PCM ADC Rate (record sample rate; requires VRA)
 *
 * Capture configuration: 8 kHz, 8-bit, mono (left channel only)
 *   — Matches KCS decoding parameters (7 samples per bit period at 8 kHz)
 *
 * Design constraints (bare-metal inteilidOS):
 *   • No stdlib, no libc — k-prefixed helpers only
 *   • No 64-bit arithmetic
 *   • No floating point
 *   • Static buffers only (DMA memory must be physically contiguous —
 *     static BSS satisfies this in a flat ring-0 binary)
 *   • Must compile clean under both -march=pentium3 and -march=pentium2
 *
 * KCS decoding at 8000 Hz:
 *   Samples per bit period = 8000/1200 ≈ 7
 *   0-bit (1200 Hz): ~2 zero-crossings in 7 samples
 *   1-bit (2400 Hz): ~4 zero-crossings in 7 samples
 *   Decision threshold: ≥ 3 crossings → 1-bit, < 3 → 0-bit
 */

#include "ac97.h"
#include "pci.h"
#include "timer.h"
#include "memory.h"
#include <stdint.h>

/* =========================================================================
 * MMIO helpers — volatile pointer access into the CS4281/AC'97 register space
 * ========================================================================= */
static inline uint8_t  ac97_rd8 (uint32_t base, uint32_t off) {
    return *(volatile uint8_t  *)(uintptr_t)(base + off);
}
static inline uint32_t ac97_rd32(uint32_t base, uint32_t off) {
    return *(volatile uint32_t *)(uintptr_t)(base + off);
}
static inline void ac97_wr8 (uint32_t base, uint32_t off, uint8_t  v) {
    *(volatile uint8_t  *)(uintptr_t)(base + off) = v;
}
static inline void ac97_wr32(uint32_t base, uint32_t off, uint32_t v) {
    *(volatile uint32_t *)(uintptr_t)(base + off) = v;
}

/* =========================================================================
 * CS4281 BA0 register offsets
 * ========================================================================= */

/* Host interrupt */
#define BA0_HICR    0x0004u   /* Host Interrupt Control Register            */
#define BA0_HIMR    0x0008u   /* Host Interrupt Mask Register               */

/* DMA channels (A=playback=ch0, B=capture=ch1) */
#define BA0_DBA0    0x0040u   /* Playback DMA Base Address                  */
#define BA0_DBC0    0x0044u   /* Playback DMA Byte Count                    */
#define BA0_DBA1    0x0048u   /* Capture DMA Base Address                   */
#define BA0_DBC1    0x004Cu   /* Capture DMA Byte Count (size – 1)          */
#define BA0_DBI0    0x0054u   /* Playback DMA Buffer Index                  */
#define BA0_DBI1    0x0058u   /* Capture DMA Buffer Index (write position)  */
#define BA0_DMR0    0x0060u   /* DMA Mode Register 0 (playback)             */
#define BA0_DCR0    0x0068u   /* DMA Command Register 0 (playback)          */
#define BA0_DMR1    0x0064u   /* DMA Mode Register 1 (capture)              */
#define BA0_DCR1    0x006Cu   /* DMA Command Register 1 (capture)           */
#define BA0_DLSR1   0x0078u   /* DMA Last Sample Register 1 (capture)       */

/* FIFOs */
#define BA0_FCR0    0x0108u   /* FIFO Control Register 0 (playback)         */
#define BA0_FCR1    0x010Cu   /* FIFO Control Register 1 (capture)          */
#define BA0_FPDR0   0x0110u   /* FIFO Port Data Register 0 (playback)       */
#define BA0_FPDR1   0x0114u   /* FIFO Port Data Register 1 (capture)        */

/* Clock / serial */
#define BA0_CLKCR1  0x0400u   /* Clock Control Register 1                   */
#define BA0_CLKCR2  0x0404u   /* Clock Control Register 2                   */
#define BA0_PLLM    0x0408u   /* PLL M Register                             */
#define BA0_PLLD    0x040Cu   /* PLL D Register                             */
#define BA0_SERMC1  0x0420u   /* Serial Port Master Control Register 1      */
#define BA0_SERMC2  0x0424u   /* Serial Port Master Control Register 2      */
#define BA0_SERC1   0x0428u   /* Serial Port Configuration Register 1       */

/* AC-Link codec access */
#define BA0_ACOSV   0x0468u   /* AC Output Slot Valid Register              */
#define BA0_ACCAD   0x046Cu   /* AC Codec Address Register (write addr)     */
#define BA0_ACCDA   0x0470u   /* AC Codec Data Register (write data)        */
#define BA0_ACISV   0x0474u   /* AC Input Slot Valid Register               */
#define BA0_ACSAD   0x0478u   /* AC Status Address Register                 */
#define BA0_ACSDA   0x047Cu   /* AC Status Data Register                    */
#define BA0_JSPT    0x0480u   /* Joystick Poll/Trigger Register             */

/* Sound system control */
#define BA0_SSCR    0x0800u   /* Sound System Control Register              */
#define BA0_SRCSA   0x0804u   /* SRC Slot Assignment Register               */
#define BA0_MULT0   0x0808u   /* Multiplier 0                               */
#define BA0_MULT1   0x080Cu   /* Multiplier 1                               */
#define BA0_SRSA    0x0818u   /* Source A Sample Rate                       */
#define BA0_SRSB    0x081Cu   /* Source B (capture) Sample Rate             */

/* ── CLKCR1 bits ─────────────────────────────────────────────────────────── */
#define CLKCR1_PLLP  (1u << 0)   /* PLL power-up                            */
#define CLKCR1_SWCE  (1u << 1)   /* Switch clock enable (use PLL output)    */
#define CLKCR1_CKRA  (1u << 16)  /* Clock run acknowledge                   */

/* ── SERMC1 bits ─────────────────────────────────────────────────────────── */
#define SERMC1_MSPE  (1u << 0)   /* Master serial port enable               */
#define SERMC1_PTC   (7u << 1)   /* Port timing control mask (AC'97 = 001)  */
#define SERMC1_PTC_AC97 (1u << 1)/* AC'97 timing                            */

/* ── FCR bits ────────────────────────────────────────────────────────────── */
#define FCR_FEN      (1u << 31)  /* FIFO Enable                             */
#define FCR_DACZ     (1u << 30)  /* DAC Zero (mute when FIFO empty)         */
#define FCR_PSH      (1u << 29)  /* FIFO push (read: new data available)    */
#define FCR_RS(x)    ((x) & 0x1Fu)       /* Right slot select               */
#define FCR_LS(x)    (((x) & 0x1Fu) << 8)/* Left slot select                */
#define FCR_SZ(x)    (((x) & 0x7Fu) << 16)/* FIFO size - 1                 */
#define FCR_OF(x)    (((x) & 0x7Fu) << 24)/* FIFO base offset               */

/* ── DMR bits ────────────────────────────────────────────────────────────── */
#define DMR_DMA      (1u << 29)  /* DMA enable                              */
#define DMR_POLL     (1u << 28)  /* Poll mode (no interrupt)                */
#define DMR_TIE      (1u << 27)  /* Terminal count interrupt enable         */
#define DMR_FEIE     (1u << 26)  /* FIFO error interrupt enable             */
#define DMR_FOIE     (1u << 25)  /* FIFO overrun interrupt enable           */
#define DMR_FUIE     (1u << 24)  /* FIFO underrun interrupt enable          */
#define DMR_TYPE_AUTO (2u << 20) /* DMA type: auto-increment                */
#define DMR_SIZE8    (1u << 14)  /* Transfer size 8-bit                     */
#define DMR_MONO     (1u << 13)  /* Mono                                    */

/* ── DCR bits ────────────────────────────────────────────────────────────── */
#define DCR_HTCIE    (1u << 17)  /* Half-terminal count interrupt enable     */
#define DCR_TCIE     (1u << 16)  /* Terminal count interrupt enable          */
#define DCR_MSK      (1u << 0)   /* DMA mask (paused when set)              */

/* ── SSCR bits ───────────────────────────────────────────────────────────── */
#define SSCR_HVS0    (1u << 23)  /* HVATX volume strobe                     */
#define SSCR_MVCS0   (1u << 22)  /* Master volume change strobe             */

/* ── AC'97 codec register addresses ─────────────────────────────────────── */
#define AC97_RESET      0x00u    /* Reset                                   */
#define AC97_RECGAIN    0x1Cu    /* Record Gain (ADC capture gain)          */
#define AC97_RECSEL     0x1Au    /* Record Select (capture source)          */
#define AC97_EXTAUDIO   0x2Au    /* Extended Audio ID                       */
#define AC97_EXTSTAT    0x2Cu    /* Extended Audio Status/Control           */
#define AC97_ADCRATE    0x38u    /* PCM ADC Sample Rate (record)            */

/* AC97_RECSEL values: which input feeds the ADC */
#define AC97_RECSEL_MIC      0x0000u   /* MIC input                        */
#define AC97_RECSEL_LINE_IN  0x0404u   /* Line-in (L+R)                    */

/* ── Codec access bits (ACCAD/ACSAD) ────────────────────────────────────── */
#define ACCAD_READ   (1u << 7)   /* Read command (set bit 7 in address)     */

/* =========================================================================
 * DMA capture buffer
 *
 * Ping-pong ring: 4 KB total, 2048 bytes per half.
 * DBI1 (write index) advances as DMA fills the buffer; we chase it.
 *
 * 128-byte alignment keeps the buffer safe for DMA on any PCI bridge.
 * ========================================================================= */
#define AC97_CAP_BUF_HALF   2048u
#define AC97_CAP_BUF_TOTAL  (AC97_CAP_BUF_HALF * 2u)

static uint8_t g_capture_buf[AC97_CAP_BUF_TOTAL]
    __attribute__((aligned(128)));

/* =========================================================================
 * Driver state
 * ========================================================================= */
static int      g_ac97_present = 0;   /* 1 after successful ac97_init()     */
static uint32_t g_ba0          = 0;   /* BA0 MMIO base (BAR0)               */
static uint32_t g_read_pos     = 0;   /* consumer offset in capture ring    */

/* =========================================================================
 * AC'97 codec verb helpers
 *
 * The CS4281 provides access to the AC'97 codec through four registers:
 *   ACCAD — write the codec register address (bit 7 = 0 for write)
 *   ACCDA — write the 16-bit codec register value
 *   ACSAD — poll until the read address matches (bit 7 = 0 = ready)
 *   ACSDA — read back the 16-bit codec register value
 *
 * Codec writes are fire-and-forget over AC-Link; we poll for completion
 * using a short delay.  Codec reads require a dummy write to ACCAD with
 * bit 7 set, then polling ACSAD until the address echoes back.
 * ========================================================================= */

static void ac97_codec_write(uint8_t reg, uint16_t val)
{
    uint32_t timeout = 100000u;

    /* Wait for any previous codec access to complete */
    while (timeout--) {
        if (!(ac97_rd32(g_ba0, BA0_ACISV) & 0x80000000u)) break;
        __asm__ volatile ("nop");
    }

    ac97_wr32(g_ba0, BA0_ACCAD, (uint32_t)(reg & 0x7Eu));
    ac97_wr32(g_ba0, BA0_ACCDA, (uint32_t)val);

    /* Allow the AC-Link frame to complete (~20 µs at 48 kHz AC-Link) */
    timer_sleep(1u);
}

static uint16_t ac97_codec_read(uint8_t reg)
{
    uint32_t timeout = 100000u;
    uint32_t acsad;

    /* Issue a read request: set bit 7 (read command) in address register */
    ac97_wr32(g_ba0, BA0_ACCAD, (uint32_t)(reg & 0x7Eu) | 0x80u);
    ac97_wr32(g_ba0, BA0_ACCDA, 0u);

    /* Poll BA0_ACSAD until bit 7 clears and address matches */
    while (timeout--) {
        acsad = ac97_rd32(g_ba0, BA0_ACSAD);
        if ((acsad & 0x80u) == 0u && (acsad & 0x7Eu) == (uint32_t)(reg & 0x7Eu))
            return (uint16_t)(ac97_rd32(g_ba0, BA0_ACSDA) & 0xFFFFu);
        __asm__ volatile ("nop");
    }
    return 0u;   /* timeout */
}

/* =========================================================================
 * ac97_init — locate, reset, and arm the CS4281 capture engine
 * ========================================================================= */
int ac97_init(void)
{
    pci_device_t pdev;
    uint32_t     bar0;
    uint32_t     timeout;
    uint16_t     extid;
    uint32_t     pa_buf;
    int          found = 0;

    if (g_ac97_present) return 1;

    /* ── 1. Find CS4281 by vendor/device ID or by AC'97 class ────────── */
    /* Try CS4281 specifically first (vendor 0x1013, device 0x4281).
     * pci_find_device searches by class; we call it with the AC'97 audio
     * class and then check the vendor/device IDs for CS4281.             */
    if (pci_find_device(0x04u, 0x01u, 0x00u, &pdev) == 0) {
        /* Check if this is a CS4281 */
        if (pdev.vendor_id == 0x1013u && pdev.device_id == 0x4281u) {
            found = 1;
        }
    }

    if (!found) {
        /* Accept any AC'97 audio controller (class 0x04 / subclass 0x01) */
        if (pci_find_device(0x04u, 0x01u, 0x00u, &pdev) == 0) {
            found = 1;
        }
    }

    if (!found) return 0;

    /* ── 2. Enable bus-master DMA ────────────────────────────────────── */
    pci_enable_busmaster(&pdev);

    /* ── 3. BA0 MMIO base from BAR0 (mask type bits[3:0]) ───────────── */
    bar0 = pdev.bar[0] & 0xFFFFFFF0u;
    if (bar0 == 0u) return 0;
    g_ba0 = bar0;

    /* ── 4. Mask all host interrupts (we use polling only) ───────────── */
    ac97_wr32(g_ba0, BA0_HIMR, 0xFFFFFFFFu);
    ac97_wr32(g_ba0, BA0_HICR, 0u);

    /* ── 5. Power up the PLL and enable the clock ────────────────────── */
    /* CS4281 requires the internal PLL to be running before AC-Link can
     * operate.  Assert PLLP, wait for lock (up to 50 ms), then enable
     * SWCE to switch the chip's clocking source to the PLL output.       */
    ac97_wr32(g_ba0, BA0_CLKCR1, CLKCR1_PLLP);
    timeout = 500u;   /* 500 × 100 µs = 50 ms max wait */
    while (timeout--) {
        if (ac97_rd32(g_ba0, BA0_CLKCR1) & CLKCR1_CKRA) break;
        timer_sleep(1u);
    }
    /* Enable PLL clock output regardless — the chip clocks itself once PLLP=1 */
    ac97_wr32(g_ba0, BA0_CLKCR1, CLKCR1_PLLP | CLKCR1_SWCE);
    timer_sleep(1u);

    /* ── 6. Enable AC-Link serial port (AC'97 timing) ───────────────── */
    ac97_wr32(g_ba0, BA0_SERMC2, 0u);
    ac97_wr32(g_ba0, BA0_SERMC1,
              SERMC1_MSPE | SERMC1_PTC_AC97);
    timer_sleep(2u);   /* AC-Link cold-reset: codec needs ≥ 1 ms           */

    /* ── 7. Reset the AC'97 codec ────────────────────────────────────── */
    ac97_codec_write(AC97_RESET, 0u);
    timer_sleep(20u);  /* allow codec warm-up */

    /* ── 8. Enable Variable Rate Audio (VRA) in the codec ───────────── */
    /* Extended Audio ID register bit 0 advertises VRA support.
     * Write 1 to bit 0 of Extended Audio Status/Control to enable VRA.   */
    extid = ac97_codec_read(AC97_EXTAUDIO);
    if (extid & 0x0001u) {
        /* VRA is supported — enable it */
        ac97_codec_write(AC97_EXTSTAT, 0x0001u);
        timer_sleep(1u);

        /* Set ADC sample rate to 8000 Hz (0x1F40) */
        ac97_codec_write(AC97_ADCRATE, 0x1F40u);
        timer_sleep(1u);
    }
    /* If VRA is not supported the codec defaults to 48 kHz; KCS will
     * still work but the crossing-count threshold must be scaled.
     * We accept 48 kHz as a fallback — the KCS decoder is tolerant.      */

    /* ── 9. Select MIC as record source and maximise capture gain ─────── */
    ac97_codec_write(AC97_RECSEL, AC97_RECSEL_MIC);
    timer_sleep(1u);
    /* Gain: 0x0000 = 0 dB (codec default); boost to near-max = 0x0F0F   */
    ac97_codec_write(AC97_RECGAIN, 0x0F0Fu);
    timer_sleep(1u);

    /* ── 10. Configure the CS4281 DMA channel B (capture) ───────────── */
    /* DMA slot assignment: capture channel uses slot 3 (left ADC) and
     * slot 4 (right ADC) on the AC-Link.  We record mono left only.
     * BA0_SRCSA bits[23:16] = capture left slot (3), bits[31:24] = right. */
    ac97_wr32(g_ba0, BA0_SRCSA, 0x03030303u);

    /* Stop DMA channel B and configure */
    ac97_wr32(g_ba0, BA0_DCR1, DCR_MSK);   /* pause DMA */
    timer_sleep(1u);

    pa_buf = (uint32_t)(uintptr_t)g_capture_buf;

    /* Set DMA base address and byte count */
    ac97_wr32(g_ba0, BA0_DBA1, pa_buf);
    ac97_wr32(g_ba0, BA0_DBC1, AC97_CAP_BUF_TOTAL - 1u);

    /* Pre-fill capture buffer with silence (0x80 = midpoint for 8-bit PCM) */
    kmemset(g_capture_buf, 0x80u, sizeof(g_capture_buf));

    /* ── 11. Configure FIFO for capture channel (FCR1) ──────────────── */
    /* FIFO sits between the AC-Link slot data and the DMA engine.
     * We configure a 16-byte FIFO (size = 15 stored as SZ field).
     * Slot 3 = left ADC (bits[4:0] = 3), slot 3 for right as well.
     * FEN enables the FIFO; DACZ is only used for playback.              */
    ac97_wr32(g_ba0, BA0_FCR1,
              FCR_FEN                   /* FIFO enable          */
            | FCR_RS(3u)               /* right slot = 3 (ADC) */
            | FCR_LS(3u)               /* left  slot = 3 (ADC) */
            | FCR_SZ(15u)              /* FIFO size = 16 bytes  */
            | FCR_OF(16u));            /* FIFO base offset      */

    /* ── 12. Set DMA mode: capture, 8-bit, mono, auto-increment ─────── */
    ac97_wr32(g_ba0, BA0_DMR1,
              DMR_TYPE_AUTO             /* auto-increment DMA   */
            | DMR_POLL                  /* polling mode         */
            | DMR_SIZE8                 /* 8-bit samples        */
            | DMR_MONO);               /* mono                 */

    /* ── 13. Start capture DMA ───────────────────────────────────────── */
    ac97_wr32(g_ba0, BA0_DCR1, 0u);   /* clear mask → DMA runs */
    timer_sleep(2u);

    g_read_pos    = 0u;
    g_ac97_present = 1;
    return 1;
}

/* =========================================================================
 * ac97_has_signal — detect audio activity above the noise floor
 *
 * Reads up to 512 samples from the portion of the ring the DMA engine has
 * most recently filled, and checks whether any sample deviates more than
 * a threshold from the 0x80 silence midpoint (8-bit unsigned PCM).
 * ========================================================================= */
int ac97_has_signal(void)
{
    uint32_t dbi, check_start, i;

    if (!g_ac97_present) {
        if (!ac97_init()) return 0;
    }

    /* DBI1 holds the current DMA write position in the ring */
    dbi = ac97_rd32(g_ba0, BA0_DBI1) & (AC97_CAP_BUF_TOTAL - 1u);

    /* Examine the 512 bytes immediately behind the DMA write head */
    check_start = (dbi >= 512u) ? (dbi - 512u) : 0u;

    for (i = check_start; i < check_start + 512u && i < AC97_CAP_BUF_TOTAL; i++) {
        uint8_t  s   = g_capture_buf[i];
        int32_t  dev = (int32_t)(uint32_t)s - 0x80;
        if (dev < 0) dev = -dev;
        if (dev > 20) return 1;   /* ±20/128 ≈ 15 % noise-floor threshold */
    }
    return 0;
}

/* =========================================================================
 * Internal helpers for KCS decoding
 *
 * These are structurally identical to the HDA KCS helpers but read from
 * the AC'97 capture ring instead.
 * ========================================================================= */

/* Wait until the DMA engine has written at least 'n' more bytes beyond
 * g_read_pos into the ring.  Returns 1 on success, 0 on timeout.          */
static int ac97_wait_samples(uint32_t n, uint32_t timeout_ms)
{
    uint32_t start = timer_get_ticks();

    while ((timer_get_ticks() - start) < timeout_ms) {
        uint32_t dbi   = ac97_rd32(g_ba0, BA0_DBI1) & (AC97_CAP_BUF_TOTAL - 1u);
        uint32_t avail;

        avail = (dbi >= g_read_pos)
                ? (dbi - g_read_pos)
                : (AC97_CAP_BUF_TOTAL - g_read_pos + dbi);

        if (avail >= n) return 1;
        __asm__ volatile ("nop");
    }
    return 0;
}

/* Consume one sample from the ring and advance g_read_pos. */
static uint8_t ac97_next_sample(void)
{
    uint8_t s = g_capture_buf[g_read_pos];
    g_read_pos = (g_read_pos + 1u) % AC97_CAP_BUF_TOTAL;
    return s;
}

/* Count 0x80-midpoint crossings in the next 'count' samples, given that
 * the sample immediately before this window was 'prev_s'.
 * Updates *prev_out to the last sample consumed.                           */
static int ac97_count_crossings(uint8_t prev_s, uint32_t count,
                                uint8_t *prev_out)
{
    int      crossings = 0;
    uint8_t  last = prev_s;
    uint32_t i;

    for (i = 0u; i < count; i++) {
        uint8_t s = ac97_next_sample();
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
static int ac97_decode_bit(uint8_t *prev_s)
{
    int     xings;
    uint8_t last;

    if (!ac97_wait_samples(7u, 20u)) return -1;

    xings = ac97_count_crossings(*prev_s, 7u, &last);
    *prev_s = last;

    return (xings >= 3) ? 1 : 0;
}

/* =========================================================================
 * ac97_capture_kcs_byte — decode one KCS-framed byte (8N1, LSB first)
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
int ac97_capture_kcs_byte(void)
{
    uint8_t  prev_s  = 0x80u;
    uint8_t  byte_val;
    int      bit, i;

    if (!g_ac97_present) {
        if (!ac97_init()) return -2;   /* no hardware — treat as timeout */
    }

    uint32_t byte_start = timer_get_ticks();  /* 2-second window per byte */

    /* ── Wait for start bit (0-bit = 1200 Hz after idle or leader) ───── */
    while ((timer_get_ticks() - byte_start) < 2000u) {
        uint8_t s;
        int32_t dev;
        int     xings;
        uint8_t after;

        if (!ac97_wait_samples(1u, 50u)) continue;
        s = ac97_next_sample();

        dev = (int32_t)(uint32_t)s - 0x80;
        if (dev < 0) dev = -dev;
        if (dev < 10) {
            /* Input is still quiet; absorb sample and keep looking */
            prev_s = s;
            continue;
        }

        /* Signal is active.  Count crossings in the next 6 samples
         * (together with 's' that's 7 samples total for one bit period). */
        if (!ac97_wait_samples(6u, 20u)) { prev_s = s; continue; }

        xings = ac97_count_crossings(s, 6u, &after);
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
        bit = ac97_decode_bit(&prev_s);
        if (bit < 0) return -2;   /* bit-period timeout — audio dropout */
        if (bit) byte_val |= (uint8_t)(1u << i);
    }

    /* ── Verify stop bit (must be a 1-bit = 2400 Hz) ─────────────────── */
    bit = ac97_decode_bit(&prev_s);
    if (bit < 0) return -2;   /* timeout waiting for stop bit */
    if (bit != 1) return -1;  /* stop bit wrong — framing error */

    return (int)(uint32_t)byte_val;
}
