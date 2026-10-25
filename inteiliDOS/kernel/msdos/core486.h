#ifndef MSDOS_CORE486_H
#define MSDOS_CORE486_H
#include <stdint.h>
#define MSDOS_486_RAM (16u * 1024u * 1024u)
int msdos_486_execute(uint8_t *ram, uint8_t *ports);
typedef struct {
    int code, exception;
    uint32_t cs, ip, cr0, cr2, ax, sp;
} msdos_486_fault_t;
/* Captured before the private CPU is freed; cleared on each entry. */
const msdos_486_fault_t *msdos_486_last_fault(void);
/* Shared, checked BIOS/device adapter in tiny.c. */
unsigned msdos_486_in(unsigned port, int wide);
void msdos_486_out(unsigned port, unsigned value, int wide);
int msdos_486_bios(uint16_t regs[12], uint16_t *ip, uint16_t *flags);
#endif
