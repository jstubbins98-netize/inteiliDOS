/* Execute the same original COM arithmetic program without DOS or a host FPU.
 * Additional tests exercise 32-bit protected-mode decoding and memory faults.
 * MPL-2.0. */
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../kernel/msdos/tiny386/i386.h"
#include "../kernel/msdos/tiny386/fpu.h"
void *kmalloc(size_t n) {return malloc(n);}
void kfree(void *p) {free(p);}
void *kmemset(void *p,int c,size_t n) {return memset(p,c,n);}
void *kmemcpy(void *p,const void *q,size_t n) {return memcpy(p,q,n);}
int kmemcmp(const void *p,const void *q,size_t n) {return memcmp(p,q,n);}
static uint32_t read32(uint8_t *r,unsigned a) {
    return r[a]|((uint32_t)r[a+1]<<8)|((uint32_t)r[a+2]<<16)|((uint32_t)r[a+3]<<24);
}
static int ferr_level,ferr_calls;
static void ferr_callback(void *unused,int level) {
    (void)unused;ferr_level=level;++ferr_calls;
}
static void write32(uint8_t *r,unsigned a,uint32_t v) {
    for (unsigned i=0;i<4;i++) r[a+i]=(uint8_t)(v>>(8*i));
}
int main(int argc,char **argv) {
    assert(argc==2);
    uint8_t *ram=calloc(1,0x1000000);assert(ram);
    CPU_CB *cb;
    CPUI386 *cpu=cpui386_new(4,(char *)ram,0x1000000,&cb);assert(cpu);
    assert(cpui386_enable_fpu(cpu));assert(cpui386_enable_fpu(cpu));
    FILE *fp=fopen(argv[1],"rb");assert(fp);
    assert(fread(ram+0x100,1,0x6000,fp)>100);fclose(fp);
    uint16_t regs[12]={0};regs[4]=0xfffc;
    cpui386_boot(cpu,regs,0x100);
    for (int i=0;i<100 && !read32(ram,0x7000) && !cpui386_error(cpu);i++)
        cpui386_step(cpu,1000);
    if (read32(ram,0x7000)!=0x8748600d || cpui386_error(cpu)) {
        CPUI386_State s;cpui386_get_state(cpu,&s);
        fprintf(stderr,"FAIL: x87 check %u marker=%x cpu=%d at %x:%x exc=%d\n",
            read32(ram,0x7004)&65535,read32(ram,0x7000),cpui386_error(cpu),
            s.seg[1],s.ip,cpui386_get_excno(cpu));
        return 1;
    }
    puts("PASS: DOS-program arithmetic, 80-bit/24-bit precision, rounding, conversions, BCD, stack/tags, 14/28/94/108-byte state, #MF, transcendental smoke");
    /* 32-bit protected mode: FILD, FSQRT, FISTP, 108-byte FNSAVE/FRSTOR. */
    const uint8_t pm[]={
        0xdb,5,0,8,0,0,           /* fild dword [800] */
        0xd9,0xfa,                /* fsqrt */
        0xdd,0x35,0,9,0,0,        /* fnsave [900] */
        0xdd,0x25,0,9,0,0,        /* frstor [900] */
        0xdb,0x1d,4,8,0,0,        /* fistp dword [804] */
        0xf4};
    cpui386_reset_pm(cpu,0x100);cpui386_set_a20(cpu,1);
    memcpy(ram+0x100,pm,sizeof(pm));memset(ram+0x800,0,0x200);write32(ram,0x800,81);
    cpui386_step(cpu,100);
    assert(!cpui386_error(cpu) && read32(ram,0x804)==9);
    assert((read32(ram,0x900)&65535)==0x37f);
    assert(read32(ram,0x90c)==0x106);
    assert((read32(ram,0x910)&65535)==8 &&
           (read32(ram,0x910)>>16)==0x1fa);
    assert(read32(ram,0x914)==0x800);
    puts("PASS: 32-bit protected-mode x87 arithmetic and saved state");
    /* Reset must initialize an enabled FPU, not keep the previous stack/CW. */
    const uint8_t reset[]={0xd9,0x3d,0,8,0,0,0xdd,0x3d,4,8,0,0,0xf4};
    cpui386_reset_pm(cpu,0x100);memcpy(ram+0x100,reset,sizeof(reset));
    cpui386_step(cpu,100);
    assert((read32(ram,0x800)&65535)==0x37f && read32(ram,0x804)==0);
    puts("PASS: CPU reset also resets the enabled x87");
    /* CR0.EM/TS cause #NM before executing the FPU, including FNINIT.
     * FWAIT traps only for MP & TS. Real-mode handlers skip the faulting
     * two-byte ESC or the one-byte WAIT, with ordinary IRET semantics. */
    const uint8_t nmhandler[]={0xff,0x06,0x00,0x08,0x55,0x89,0xe5,
                               0x83,0x46,2,2,0x5d,0xcf};
    const uint8_t nmcode[]={0x0f,0x20,0xc0,0x0c,4,0x0f,0x22,0xc0,
                           0xdb,0xe3,0xf4};
    for (int ts=0;ts<2;ts++) {
        cpui386_reset(cpu);memset(ram+0x800,0,8);
        memcpy(ram+0x100,nmcode,sizeof(nmcode));ram[0x104]=ts?8:4;
        memcpy(ram+0x600,nmhandler,sizeof(nmhandler));write32(ram,7*4,0x600);
        cpui386_boot(cpu,regs,0x100);cpui386_step(cpu,100);cpui386_step(cpu,100);
        assert(!cpui386_error(cpu) && read32(ram,0x800)==1);
    }
    const uint8_t waitcode[]={0x0f,0x20,0xc0,0x0c,0x0a,0x0f,0x22,0xc0,
                             0x9b,0xf4};
    cpui386_reset(cpu);memset(ram+0x800,0,8);
    memcpy(ram+0x100,waitcode,sizeof(waitcode));
    memcpy(ram+0x600,nmhandler,sizeof(nmhandler));ram[0x60a]=1;
    cpui386_boot(cpu,regs,0x100);cpui386_step(cpu,100);cpui386_step(cpu,100);
    assert(!cpui386_error(cpu) && read32(ram,0x800)==1);
    puts("PASS: enabled-x87 CR0.EM/TS and FWAIT MP/TS #NM");
    /* NE=0 asserts the virtual FERR# bridge and stalls a waiting instruction
     * until the handler uses a no-wait clear. NE=1 is covered by the COM. */
    const uint8_t legacy[]={
        0xdb,0xe3,                       /* fninit */
        0xd9,0x2e,0,8,                   /* fldcw [800] */
        0xd9,0xe8,0xd9,0xee,0xde,0xf9,  /* 1 / 0 */
        0xdf,0xe0,0x9b,0xf4};           /* fnstsw ax; wait; hlt */
    cpui386_reset(cpu);cb->fpu_error=ferr_callback;
    ram[0x800]=0x7b;ram[0x801]=3;memcpy(ram+0x100,legacy,sizeof(legacy));
    cpui386_boot(cpu,regs,0x100);cpui386_step(cpu,100);
    CPUI386_State state;cpui386_get_state(cpu,&state);
    assert(!cpui386_error(cpu) && ferr_level && ferr_calls &&
           (state.gpr[0]&0x84)==0x84 && state.ip==0x10e);
    const uint8_t clear[]={0xdb,0xe2,0xf4};
    memcpy(ram+0x10e,clear,sizeof(clear));cpui386_step(cpu,100);
    assert(!cpui386_error(cpu) && !ferr_level);
    puts("PASS: legacy NE=0 FERR# assertion, WAIT stall and no-wait clear");
    /* Direct FPU transactions still use the CPU's actual paged-memory API.
     * A ten-byte operand crosses into a missing guest page; no stack change
     * may survive #PF. This also catches accidental unchecked host reads. */
    cpui386_reset_pm(cpu,0x100);memset(ram+0x1000,0,0x2000);
    write32(ram,0x1000,0x2003);
    for (unsigned i=0;i<5;i++) write32(ram,0x2000+4*i,0x1000*i+3);
    const uint8_t paging[]={0xb8,0,0x10,0,0,0x0f,0x22,0xd8,
        0x0f,0x20,0xc0,0x0d,0,0,0,0x80,0x0f,0x22,0xc0,0xf4};
    memcpy(ram+0x100,paging,sizeof(paging));cpui386_step(cpu,100);
    assert(!cpui386_error(cpu));
    FPU *f=fpu_new();assert(f);
    assert(fpu_exec1(f,cpu,1,5,0)); /* FLD1 */
    assert(!fpu_exec2(f,cpu,false,3,5,3,0x4ffc)); /* FLD m80 => #PF */
    assert(cpui386_get_excno(cpu)==14);
    cpui386_get_state(cpu,&state);assert(state.cr2==0x5000);
    assert(fpu_exec2(f,cpu,false,5,6,3,0x800)); /* FNSAVE */
    assert(((read32(ram,0x804)>>11)&7)==7 && (read32(ram,0x808)&65535)==0x3fff);
    assert(read32(ram,0x800+28)==0 && read32(ram,0x800+32)==0x80000000u);
    /* FSTP across the same boundary must not pop on a failed store. */
    assert(fpu_exec2(f,cpu,false,5,4,3,0x800));
    assert(!fpu_exec2(f,cpu,false,3,7,3,0x4ffc));
    assert(fpu_exec2(f,cpu,false,5,6,3,0x900));
    assert(((read32(ram,0x904)>>11)&7)==7 && (read32(ram,0x908)&65535)==0x3fff);
    fpu_delete(f);
    puts("PASS: cross-page x87 load/store #PF preserves stack and bounds guest access");
    cpui386_delete(cpu);free(ram);
    return 0;
}
