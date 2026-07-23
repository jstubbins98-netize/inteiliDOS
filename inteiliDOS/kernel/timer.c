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

/* Play a tone at freq_hz for duration_ms milliseconds, then silence. */
void speaker_beep(uint32_t freq_hz, uint32_t duration_ms) {
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
