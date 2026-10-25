#ifndef MSDOS_FM_H
#define MSDOS_FM_H
#include <stdint.h>
/* Private OPL model, no native I/O or guest IRQ delivery. */
void msdos_fm_reset(unsigned model);
int msdos_fm_in(unsigned port,uint8_t *value);
int msdos_fm_out(unsigned port,uint8_t value);
void msdos_fm_sample(int16_t stereo[2]);
#endif
