/*
 * inteilidOS -- kernel/idt.c
 * Interrupt Descriptor Table
 */

#include "idt.h"
#include <stdint.h>

#define IDT_ENTRIES 256

typedef struct {
    uint16_t base_lo;
    uint16_t sel;
    uint8_t  always0;
    uint8_t  flags;
    uint16_t base_hi;
} __attribute__((packed)) idt_entry_t;

typedef struct {
    uint16_t limit;
    uint32_t base;
} __attribute__((packed)) idt_ptr_t;

static idt_entry_t idt[IDT_ENTRIES];
static idt_ptr_t   idtp;

extern void idt_load(uint32_t);

void idt_set_gate(uint8_t num, uint32_t base, uint16_t sel, uint8_t flags) {
    idt[num].base_lo  = (uint16_t)(base & 0xFFFF);
    idt[num].base_hi  = (uint16_t)((base >> 16) & 0xFFFF);
    idt[num].sel      = sel;
    idt[num].always0  = 0;
    idt[num].flags    = flags;
}

void idt_init(void) {
    idtp.limit = (uint16_t)(sizeof(idt_entry_t) * IDT_ENTRIES - 1);
    idtp.base  = (uint32_t)&idt;
    __builtin_memset(&idt, 0, sizeof(idt));
    idt_load((uint32_t)&idtp);
}
