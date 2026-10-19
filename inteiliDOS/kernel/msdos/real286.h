#ifndef MSDOS_REAL286_H
#define MSDOS_REAL286_H
#include <stdint.h>

/* Real-address-mode compatibility only, not a protected-mode CPU core. */
struct real286 {
    uint32_t gdt_base, idt_base;
    uint16_t gdt_limit, idt_limit, msw;
};
enum {
    REAL286_OK=0, REAL286_UD=-1, REAL286_PROTECTED=-3
};
void real286_reset(struct real286 *cpu);
uint16_t real286_flags(unsigned flags);
/* Group 0F01 or CLTS (0F06). Memory operands are little-endian, six bytes. */
int real286_system(struct real286 *cpu, unsigned opcode, unsigned group,
                   int is_register, uint8_t operand[6]);
/* Preserves flags for zero counts; applies the 286's five-bit count mask. */
uint16_t real286_shift(uint16_t value, unsigned wide, unsigned operation,
                      unsigned count, uint16_t *flags);
#endif
