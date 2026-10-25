/* Adapter PIC tests; link with function/data sections and --gc-sections.
 * No guest binaries, native devices or physical port accesses are needed. */
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../kernel/msdos/core486.c"

void *kmalloc(size_t n) {return malloc(n);}
void kfree(void *p) {free(p);}
void *kmemset(void *p,int c,size_t n) {return memset(p,c,n);}
void *kmemcpy(void *p,const void *q,size_t n) {return memcpy(p,q,n);}
int kmemcmp(const void *p,const void *q,size_t n) {return memcmp(p,q,n);}
unsigned msdos_486_in(unsigned p,int wide) {(void)wide;return ports[p];}
void msdos_486_out(unsigned p,unsigned value,int wide) {
    (void)wide;ports[p]=(uint8_t)value;
}
static void remap(unsigned base,unsigned mask) {
    write8(NULL,0x20,0x11);
    write8(NULL,0x21,base);
    write8(NULL,0x21,4);
    write8(NULL,0x21,1);
    write8(NULL,0x21,mask);
}
int main(void) {
    ram=calloc(1,MSDOS_486_RAM);ports=calloc(1,65536);
    assert(ram && ports);
    CPU_CB *cb;
    cpu=cpui386_new(4,(char *)ram,MSDOS_486_RAM,&cb);assert(cpu);
    pic_base=8;pending_irq=0xa;pending_line=0;
    /* The CPU queued IRQ0 under real-mode CLI; the extender entered
     * protected mode and remapped the PIC before enabling interrupts. */
    cpui386_reset_pm(cpu,0x100);
    remap(0x88,0xfc);
    assert(read8(NULL,0x20)==1);
    assert(irq(NULL)==0x88 && pending_line==-1 && pending_irq==-1);
    write8(NULL,0x20,0x0b);assert(read8(NULL,0x20)==1);
    write8(NULL,0x20,0x20);assert(read8(NULL,0x20)==0);
    /* Pending keyboard and sound lines must follow a later remap too. */
    pending_irq=0x89;pending_line=1;remap(0x50,0xfc);
    assert(irq(NULL)==0x51);
    write8(NULL,0x20,0x61);assert(!pic_service);
    pending_irq=0x55;pending_line=5;remap(0xa0,0xdc);
    assert(irq(NULL)==0xa5);
    write8(NULL,0x20,0x65);assert(!pic_service);
    /* Masking between queue and acknowledgement cannot inject vector FFh. */
    pending_irq=0xa0;pending_line=0;write8(NULL,0x21,255);
    assert(irq(NULL)==-1 && !pic_service && pending_line==-1);
    /* A return to the private real-mode ROM restores its mailbox protocol,
     * rather than injecting architectural protected-mode exceptions. */
    cpui386_reset(cpu);
    pending_irq=0x88;pending_line=0;assert(irq(NULL)==0xa);
    pending_irq=0x89;pending_line=1;assert(irq(NULL)==7);
    /* A protected timer handler periodically chains its saved BIOS IRQ0.
     * The polling ROM needs an EOI only for this actual hardware path. */
    putdword(0x46c,100);pic_service=1;
    timer_bios();assert(word(0x46c)==101 && !pic_service);
    timer_bios();assert(word(0x46c)==101); /* private polling: no double tick */
    putdword(0x46c,0x1800af);pic_service=1;
    timer_bios();assert(!word(0x46c) && !word(0x46e) && ram[0x470]==1);
    /* FERR# is isolated guest IRQ13, including a remapped slave PIC and
     * both cascade masks. EOI on the slave must not touch native hardware. */
    remap(0x20,0xfb);
    write8(NULL,0xa0,0x11);write8(NULL,0xa1,0x28);
    write8(NULL,0xa1,2);write8(NULL,0xa1,1);write8(NULL,0xa1,0xdf);
    fpu_error(NULL,1);assert(read8(NULL,0xa0)==32);
    pending_line=13;pending_irq=0x75;
    assert(irq(NULL)==0x2d && pic_service==4 && slave_service==32);
    write8(NULL,0xa0,0x0b);assert(read8(NULL,0xa0)==32);
    write8(NULL,0xa0,0x20);write8(NULL,0x20,0x20);
    assert(!slave_service && !pic_service);
    write8(NULL,0xa1,255);pending_line=13;pending_irq=0x2d;
    ferr_acked=1;assert(irq(NULL)==-1 && !ferr_acked);
    write8(NULL,0xf0,0);assert(ferr_acked);
    fpu_error(NULL,0);assert(!ferr && !ferr_acked);
    /* An early exception must survive CPU disposal for the shell report. */
    cpui386_reset(cpu);
    uint16_t boot[12]={0},ip=0x100,flags=2;
    boot[0]=0x1234;boot[4]=0x9000;
    cpui386_real_state(cpu,boot,&ip,&flags,1);
    memset(ram,0,0x100);
    ram[0x100]=0x0f;ram[0x101]=0x0b; /* UD2, with no guest #UD handler. */
    cpui386_step(cpu,1);
    assert(cpui386_error(cpu));
    capture_fault();
    cpui386_delete(cpu);cpu=NULL;
    const msdos_486_fault_t *fault=msdos_486_last_fault();
    assert(fault->code==-2 && fault->exception==6);
    assert(fault->cs==0 && fault->ip==0x100 && fault->ax==0x1234);
    assert(fault->sp==0x9000 && !(fault->cr0&1));
    free(ram);free(ports);
    puts("PASS: delayed IRQ0/1/5, PIC remapping/masks/EOI, BIOS tick/midnight, isolated FERR/IRQ13 slave PIC");
    return 0;
}
