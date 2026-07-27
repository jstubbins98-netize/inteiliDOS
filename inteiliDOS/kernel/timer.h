#ifndef TIMER_H
#define TIMER_H

#include <stdint.h>

void     timer_init(uint32_t frequency_hz);
uint32_t timer_get_ticks(void);
void     timer_sleep(uint32_t ms);

/* Register a secondary callback invoked on every timer tick (after ticks++).
 * Used by the USB keyboard driver to poll the interrupt endpoint.
 * Pass NULL to unregister.  Only one secondary callback is supported.   */
void     timer_register_secondary(void (*cb)(void));

/* PC speaker — square-wave tones */
void     speaker_on(uint32_t freq_hz);
void     speaker_off(void);
void     speaker_beep(uint32_t freq_hz, uint32_t duration_ms);
void     speaker_boot_chime(void);

/* PC speaker — 8-bit PCM playback via PIT PWM.
 * Plays buf[0..len-1] at SAM_PCM_RATE (22050 Hz) through the PC speaker.
 * Blocks until playback is complete.  Restores the system timer afterwards. */
void     speaker_play_pcm(const unsigned char *buf, int len);

#endif /* TIMER_H */
