#ifndef GDT_H
#define GDT_H

#include <stdint.h>

/* GDT segment selectors */
#define GDT_SEG_NULL   0x00
#define GDT_SEG_KCODE  0x08
#define GDT_SEG_KDATA  0x10
#define GDT_SEG_UCODE  0x18
#define GDT_SEG_UDATA  0x20
#define GDT_SEG_TSS    0x28

void gdt_init(void);
/* Set the ring-0 stack used on entry from v86; returns the previous stack. */
uint32_t gdt_set_kernel_stack(uint32_t esp0);

#endif /* GDT_H */
