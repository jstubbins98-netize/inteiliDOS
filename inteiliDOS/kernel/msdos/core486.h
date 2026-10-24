#ifndef MSDOS_CORE486_H
#define MSDOS_CORE486_H
#include <stdint.h>
#define MSDOS_486_RAM (16u * 1024u * 1024u)
int msdos_486_execute(uint8_t *ram, uint8_t *ports);
/* Shared, checked BIOS/device adapter in tiny.c. */
unsigned msdos_486_in(unsigned port, int wide);
void msdos_486_out(unsigned port, unsigned value, int wide);
int msdos_486_bios(uint16_t regs[12], uint16_t *ip, uint16_t *flags);
#endif
