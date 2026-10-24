/* Software x87 for the generation-4 interpreter. MPL-2.0.
 * All arithmetic is integer-only Berkeley SoftFloat, not the native FPU. */
#ifndef MSDOS_X87_H
#define MSDOS_X87_H
#include "i386.h"
typedef struct FPU FPU;
FPU *fpu_new(void);
void fpu_delete(FPU *);
void fpu_reset(FPU *);
bool fpu_pending(FPU *);
bool fpu_nowait(int op, int group, int reg, bool memory);
void fpu_record(FPU *, uint32_t ip, uint16_t cs, uint16_t opcode,
                uint32_t data, uint16_t ds, bool memory);
bool fpu_exec1(FPU *, CPUI386 *, int op, int group, int reg);
bool fpu_exec2(FPU *, CPUI386 *, bool opsz16, int op, int group,
               int seg, uword addr);
#endif
