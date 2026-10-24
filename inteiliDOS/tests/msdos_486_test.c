/* CPU-only host checks; no DOS binaries or physical devices are needed. */
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "../kernel/msdos/tiny386/i386.h"
void *kmalloc(size_t n) {return malloc(n);}
void kfree(void *p) {free(p);}
void *kmemset(void *p,int c,size_t n) {return memset(p,c,n);}
void *kmemcpy(void *p,const void *q,size_t n) {return memcpy(p,q,n);}
int kmemcmp(const void *p,const void *q,size_t n) {return memcmp(p,q,n);}
static uint32_t load32(uint8_t *r,unsigned a) {
    return r[a]|((uint32_t)r[a+1]<<8)|((uint32_t)r[a+2]<<16)|((uint32_t)r[a+3]<<24);
}
static int irq_calls;
static int masked_irq(void *unused) {
    (void)unused;
    ++irq_calls;
    return -1;
}
int main(int argc,char **argv) {
    assert(argc==2);
    uint8_t *ram=calloc(1,16*1024*1024);
    assert(ram);
    FILE *f=fopen(argv[1],"rb");assert(f);
    size_t n=fread(ram+0x100,1,0x7000,f);fclose(f);assert(n>100);
    CPU_CB *cb;
    CPUI386 *cpu=cpui386_new(4,(char *)ram,16*1024*1024,&cb);
    assert(cpu);
    cpui386_set_a20(cpu,1);
    uint16_t regs[12]={0};cpui386_boot(cpu,regs,0x100);
    for (int i=0;i<10000 && !load32(ram,0x700);i++) {
        cpui386_step(cpu,1000);
        if (cpui386_error(cpu)) {
            CPUI386_State s;cpui386_get_state(cpu,&s);
            fprintf(stderr,"CPU error %d at %x:%x cr0=%x cr2=%x cr3=%x exc=%d bytes=%02x %02x\n",
                    cpui386_error(cpu),s.seg[1],s.ip,s.cr0,s.cr2,s.cr3,
                    cpui386_get_excno(cpu),ram[s.ip],ram[s.ip+1]);
        }
        assert(!cpui386_error(cpu));
    }
    assert(load32(ram,0x700)==0x486600d);
    CPUI386_State state;cpui386_get_state(cpu,&state);
    assert(!(state.cr0&0x80000001u));
    assert(load32(ram,0x704)==0x5046);
    puts("PASS: 486 instructions, 32-bit protected mode, paging/aliasing, #PF/retry, dirty bits, real-mode return");
    /* Huge REP must yield, not block F8/audio for billions of iterations. */
    const uint8_t rep[]={0xb9,0xff,0xff,0xff,0xff,0xbf,0x00,0x00,0x20,0x00,
                         0xb0,0x5a,0xf3,0xaa,0xf4};
    memcpy(ram+0x100,rep,sizeof(rep));
    cpui386_reset_pm(cpu,0x100);
    clock_t before=clock();
    cpui386_step(cpu,1000);
    assert(!cpui386_error(cpu));
    cpui386_get_state(cpu,&state);
    assert(state.gpr[1]>0xff000000u && ram[0x200000]==0x5a);
    assert((double)(clock()-before)/CLOCKS_PER_SEC<2.0);
    puts("PASS: bounded, restartable 32-bit REP");
    /* DJGPP probes an absent x87 before installing its #NM emulation.
     * Reset must not force CR0.EM; FNSTSW must leave its sentinel alone. */
    const uint8_t probe[]={0xc7,0x06,0x20,0x07,0x5a,0x5a,
                           0xdb,0xe3,0xdd,0x3e,0x20,0x07,0xf4};
    memcpy(ram+0x100,probe,sizeof(probe));
    cpui386_reset(cpu);cpui386_get_state(cpu,&state);
    assert(state.cr0==0x10);
    memset(regs,0,sizeof(regs));regs[4]=0x9000;
    cpui386_boot(cpu,regs,0x100);cpui386_step(cpu,100);
    assert(!cpui386_error(cpu) && (load32(ram,0x720)&65535)==0x5a5a);
    /* Software may still enable EM and receive a genuine #NM. */
    const uint8_t em[]={0x0f,0x20,0xc0,0x0c,4,0x0f,0x22,0xc0,
                        0xdb,0xe3,0xf4};
    const uint8_t nm[]={0xff,0x06,0x24,0x07,0x55,0x89,0xe5,
                        0x83,0x46,2,2,0x5d,0xcf};
    memcpy(ram+0x100,em,sizeof(em));memcpy(ram+0x200,nm,sizeof(nm));
    ram[7*4]=0;ram[7*4+1]=2;ram[7*4+2]=ram[7*4+3]=0;
    cpui386_reset(cpu);cpui386_boot(cpu,regs,0x100);
    cpui386_step(cpu,100);cpui386_step(cpu,100);
    assert(!cpui386_error(cpu) && load32(ram,0x724)==1);
    puts("PASS: absent-x87 startup probe and software-enabled #NM/IRET");
    /* A queued line can be masked before acknowledgement. -1 is not an
     * interrupt vector, and must not touch a protected-mode stack/IDT. */
    const uint8_t idle[]={0xfb,0x90,0xf4,
                          0xc7,5,0x28,7,0,0,1,0,0,0};
    memcpy(ram+0x100,idle,sizeof(idle));cpui386_reset_pm(cpu,0x100);
    cb->pic_read_irq=masked_irq;cpui386_raise_irq(cpu);
    cpui386_step(cpu,100);cpui386_step(cpu,100);
    assert(irq_calls==1 && !cpui386_error(cpu));
    cpui386_raise_irq(cpu);cpui386_step(cpu,100);
    assert(irq_calls==2 && !load32(ram,0x728) && !cpui386_error(cpu));
    puts("PASS: masked pending interrupt acknowledgement is not vector -1");
    /* A bogus page-directory must not index host memory outside guest RAM. */
    const uint8_t bad[]={0xb8,0x00,0xf0,0xff,0xff,0x0f,0x22,0xd8,
                        0x0f,0x20,0xc0,0x0d,0x00,0x00,0x00,0x80,0x0f,0x22,0xc0};
    memcpy(ram+0x100,bad,sizeof(bad));cpui386_reset_pm(cpu,0x100);
    for (int i=0;i<10 && !cpui386_error(cpu);i++) cpui386_step(cpu,100);
    assert(cpui386_error(cpu));
    puts("PASS: invalid protected-mode paging fails inside the emulator");
    cpui386_delete(cpu);free(ram);
    return 0;
}
