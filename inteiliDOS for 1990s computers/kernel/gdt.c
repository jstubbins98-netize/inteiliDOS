/*
 * inteilidOS -- kernel/gdt.c
 * Global Descriptor Table (flat 32-bit model + TSS)
 */

#include "gdt.h"
#include <stdint.h>

#define GDT_ENTRIES 6

typedef struct {
    uint16_t limit_low;
    uint16_t base_low;
    uint8_t  base_middle;
    uint8_t  access;
    uint8_t  granularity;
    uint8_t  base_high;
} __attribute__((packed)) gdt_entry_t;

typedef struct {
    uint16_t limit;
    uint32_t base;
} __attribute__((packed)) gdt_ptr_t;

/* Task State Segment (minimal) */
typedef struct {
    uint32_t prev_tss;
    uint32_t esp0;   /* kernel stack */
    uint32_t ss0;
    uint32_t esp1, ss1, esp2, ss2;
    uint32_t cr3, eip, eflags;
    uint32_t eax, ecx, edx, ebx, esp, ebp, esi, edi;
    uint32_t es, cs, ss, ds, fs, gs;
    uint32_t ldt;
    uint16_t trap, iomap_base;
} __attribute__((packed)) tss_entry_t;

static gdt_entry_t gdt[GDT_ENTRIES];
static gdt_ptr_t   gdtp;
static tss_entry_t tss;

extern void gdt_flush(uint32_t);
extern void tss_flush(void);

/* Build one GDT entry */
static void gdt_set_gate(int num, uint32_t base, uint32_t limit,
                          uint8_t access, uint8_t gran) {
    gdt[num].base_low    = (uint16_t)(base & 0xFFFF);
    gdt[num].base_middle = (uint8_t)((base >> 16) & 0xFF);
    gdt[num].base_high   = (uint8_t)((base >> 24) & 0xFF);
    gdt[num].limit_low   = (uint16_t)(limit & 0xFFFF);
    gdt[num].granularity = (uint8_t)(((limit >> 16) & 0x0F) | (gran & 0xF0));
    gdt[num].access      = access;
}

static void write_tss(int num, uint16_t ss0, uint32_t esp0) {
    uint32_t base  = (uint32_t)&tss;
    uint32_t limit = sizeof(tss) - 1;
    /* gran=0x00: G=0, D/B=0 — reserved bits must be 0 for TSS descriptors */
    gdt_set_gate(num, base, limit, 0x89, 0x00);

    __builtin_memset(&tss, 0, sizeof(tss));
    tss.ss0  = ss0;
    tss.esp0 = esp0;
    tss.cs   = GDT_SEG_KCODE | 0x3;
    tss.ss = tss.ds = tss.es = tss.fs = tss.gs = GDT_SEG_KDATA | 0x3;
}

void gdt_init(void) {
    gdtp.limit = (uint16_t)(sizeof(gdt_entry_t) * GDT_ENTRIES - 1);
    gdtp.base  = (uint32_t)&gdt;

    gdt_set_gate(0, 0, 0, 0, 0);                /* null descriptor */
    gdt_set_gate(1, 0, 0xFFFFFFFF, 0x9A, 0xCF); /* kernel code     */
    gdt_set_gate(2, 0, 0xFFFFFFFF, 0x92, 0xCF); /* kernel data     */
    gdt_set_gate(3, 0, 0xFFFFFFFF, 0xFA, 0xCF); /* user code       */
    gdt_set_gate(4, 0, 0xFFFFFFFF, 0xF2, 0xCF); /* user data       */
    write_tss(5, GDT_SEG_KDATA, 0);             /* TSS             */

    gdt_flush((uint32_t)&gdtp);
    tss_flush();
}
