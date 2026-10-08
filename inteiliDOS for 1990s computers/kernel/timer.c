/*
 * inteilidOS -- kernel/timer.c
 * Programmable Interval Timer (PIT 8253/8254) driver + PC speaker
 */

#include "timer.h"
#include "isr.h"
#include "vga.h"
#include <stdint.h>

#define PIT_CH0      0x40   /* channel 0 data port          */
#define PIT_CH2      0x42   /* channel 2 data port (speaker) */
#define PIT_CMD      0x43   /* mode/command register         */
#define PIT_BASE_HZ  1193182u

#define SPEAKER_PORT 0x61   /* PC speaker gate               */

static volatile uint32_t ticks = 0;

/* Optional secondary callback registered by usb_keyboard_init().
 * Called once per tick (1 kHz) after ticks is incremented.              */
static void (*timer_secondary_cb)(void) = 0;

void timer_register_secondary(void (*cb)(void)) {
    timer_secondary_cb = cb;
}

static inline void outb(uint16_t port, uint8_t val) {
    __asm__ volatile ("outb %0, %1" : : "a"(val), "Nd"(port));
}

static inline uint8_t inb(uint16_t port) {
    uint8_t val;
    __asm__ volatile ("inb %1, %0" : "=a"(val) : "Nd"(port));
    return val;
}

static void timer_handler(registers_t *regs) {
    (void)regs;
    ticks++;
    if (timer_secondary_cb) timer_secondary_cb();
}

void timer_init(uint32_t frequency_hz) {
    uint32_t divisor = PIT_BASE_HZ / frequency_hz;
    isr_register_handler(32, timer_handler);

    outb(PIT_CMD, 0x36);                    /* channel 0, lo/hi, mode 3 */
    outb(PIT_CH0, (uint8_t)(divisor & 0xFF));
    outb(PIT_CH0, (uint8_t)((divisor >> 8) & 0xFF));
}

uint32_t timer_get_ticks(void) { return ticks; }

void timer_sleep(uint32_t ms) {
    uint32_t target = ticks + ms;   /* assumes 1000 Hz tick rate */
    while (ticks < target)
        __asm__ volatile ("hlt");
}

/* ---- PC speaker ---- */

/* Global volume: 0 (silent) – 100 (full).  Default 50%.
 * volatile so the IRQ handler always reads the live value. */
static volatile int speaker_volume = 50;

void speaker_set_volume(int pct) {
    if (pct < 0)   pct = 0;
    if (pct > 100) pct = 100;
    speaker_volume = pct;
}

int speaker_get_volume(void) {
    return speaker_volume;
}

/* Start a continuous square wave at freq_hz on the PC speaker. */
void speaker_on(uint32_t freq_hz) {
    if (freq_hz == 0) return;
    uint32_t divisor = PIT_BASE_HZ / freq_hz;

    /* Programme PIT channel 2: lo/hi byte, mode 3 (square wave) */
    outb(PIT_CMD, 0xB6);
    outb(PIT_CH2, (uint8_t)(divisor & 0xFF));
    outb(PIT_CH2, (uint8_t)((divisor >> 8) & 0xFF));

    /* Gate the speaker: set bits 0 (timer-2 gate) and 1 (speaker enable) */
    outb(SPEAKER_PORT, inb(SPEAKER_PORT) | 0x03);
}

/* Stop the PC speaker. */
void speaker_off(void) {
    outb(SPEAKER_PORT, inb(SPEAKER_PORT) & ~0x03);
}

/* Play a tone at freq_hz for duration_ms milliseconds, then silence.
 * Silently skipped when volume is 0. */
void speaker_beep(uint32_t freq_hz, uint32_t duration_ms) {
    if (speaker_volume == 0) return;
    speaker_on(freq_hz);
    timer_sleep(duration_ms);
    speaker_off();
}

/*
 * Boot chime — ascending major-chord arpeggio (C5 → E5 → G5 → C6).
 * Called once from kernel_main after interrupts are enabled.
 */
void speaker_boot_chime(void) {
    speaker_beep(523,  90);   /* C5  */
    timer_sleep(20);
    speaker_beep(659,  90);   /* E5  */
    timer_sleep(20);
    speaker_beep(784,  90);   /* G5  */
    timer_sleep(20);
    speaker_beep(1047, 180);  /* C6  */
}

/* =========================================================================
 * PC-speaker 8-bit PCM playback via PIT channel 2 PWM
 *
 * Technique: IRQ0 is reprogrammed to fire at 22050 Hz.  Each interrupt
 * loads a new count into PIT channel 2 (mode 0, one-shot):
 *
 *   count = (255 - sample) * PCM_DIV / 255        (clamped ≥ 1)
 *
 * PIT ch2 mode 0: output LOW while counting, HIGH when expired.
 * Speaker gate: speaker = PIT_ch2_output AND bit1_of_0x61.
 * With both bits 0 and 1 of port 0x61 set, the speaker follows PIT ch2.
 *
 * For sample 255 → count ≈ 1  → speaker OFF ≈ 0.8 µs → ~98 % duty ✓
 * For sample 128 → count ≈ 27 → ~50 % duty                        ✓
 * For sample 0   → count = 54 → speaker OFF full period            ✓
 *
 * The system timer (ticks) is corrected for the elapsed time after
 * playback so that timer_sleep() still works correctly afterwards.
 * ========================================================================= */

#define PCM_RATE 22050u
#define PCM_DIV  (PIT_BASE_HZ / PCM_RATE)   /* ≈ 54 PIT clocks per sample */

static volatile const unsigned char *pcm_data;
static volatile int pcm_total;
static volatile int pcm_pos;
static volatile int pcm_done;

static void pcm_irq_handler(registers_t *regs) {
    (void)regs;
    unsigned int count;
    unsigned char s;

    if (pcm_pos >= pcm_total) {
        pcm_done = 1;
        return;
    }
    s = pcm_data[pcm_pos++];

    /* Apply volume: scale amplitude around midpoint 128.
     * vol=100 → s unchanged; vol=50 → half amplitude; vol=0 → silence (128). */
    {
        int sv = (int)s - 128;
        sv = sv * speaker_volume / 100;
        s = (unsigned char)(128 + sv);
    }

    /* PWM off-time: high sample → short off-time → more ON → louder */
    count = ((unsigned int)(255u - s) * PCM_DIV) / 255u;
    if (count == 0) count = 1;          /* avoid 0→65536 wrap-around */

    outb(PIT_CMD, 0xB0);                /* ch2, lo/hi byte, mode 0 */
    outb(PIT_CH2, (uint8_t)(count & 0xFF));
    outb(PIT_CH2, (uint8_t)((count >> 8) & 0xFF));
}

void speaker_play_pcm(const unsigned char *buf, int len) {
    uint32_t div1000;

    if (!buf || len <= 0) return;

    /* Disable secondary callback during playback (would fire 22× too fast) */
    void (*saved_cb)(void) = timer_secondary_cb;
    timer_secondary_cb = NULL;
    uint32_t saved_ticks = ticks;

    /* Set up PCM state */
    pcm_data  = buf;
    pcm_total = len;
    pcm_pos   = 0;
    pcm_done  = 0;

    /* Prime PIT ch2 with a mid-value count; enable gate + speaker */
    outb(PIT_CMD, 0xB0);
    outb(PIT_CH2, 27);
    outb(PIT_CH2, 0);
    outb(SPEAKER_PORT, (uint8_t)((inb(SPEAKER_PORT) & 0xFC) | 0x03));

    /* Install PCM IRQ0 handler */
    isr_register_handler(32, pcm_irq_handler);

    /* Reprogram PIT ch0 to PCM rate */
    outb(PIT_CMD, 0x36);
    outb(PIT_CH0, (uint8_t)(PCM_DIV & 0xFF));
    outb(PIT_CH0, (uint8_t)((PCM_DIV >> 8) & 0xFF));

    /* Busy-wait with HLT until all samples are played */
    while (!pcm_done)
        __asm__ volatile ("sti\n\thlt\n\tcli" ::: "memory");
    __asm__ volatile ("sti" ::: "memory");

    /* Restore PIT ch0 to 1000 Hz */
    div1000 = PIT_BASE_HZ / 1000u;
    outb(PIT_CMD, 0x36);
    outb(PIT_CH0, (uint8_t)(div1000 & 0xFF));
    outb(PIT_CH0, (uint8_t)((div1000 >> 8) & 0xFF));

    /* Restore the normal timer handler */
    isr_register_handler(32, timer_handler);

    /* Silence the speaker */
    outb(SPEAKER_PORT, (uint8_t)(inb(SPEAKER_PORT) & ~0x03));

    /* Correct ticks for elapsed playback time */
    ticks = saved_ticks + ((uint32_t)len * 1000u) / PCM_RATE;

    /* Restore secondary callback */
    timer_secondary_cb = saved_cb;
}
