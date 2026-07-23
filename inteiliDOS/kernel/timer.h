#ifndef TIMER_H
#define TIMER_H

#include <stdint.h>

void     timer_init(uint32_t frequency_hz);
uint32_t timer_get_ticks(void);
void     timer_sleep(uint32_t ms);

/* PC speaker */
void     speaker_on(uint32_t freq_hz);
void     speaker_off(void);
void     speaker_beep(uint32_t freq_hz, uint32_t duration_ms);
void     speaker_boot_chime(void);

#endif /* TIMER_H */
