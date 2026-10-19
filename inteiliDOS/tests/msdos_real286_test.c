#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "../kernel/msdos/real286.h"

#if defined(__x86_64__) && defined(__GNUC__)
/* Test only defined result/flag bits against the host's masked-count x86
 * instructions. Compile with -mno-red-zone because this uses PUSHF/POPF. */
#define NATIVE(instruction,format,value) \
    __asm__ volatile("pushq %[before]; popfq; " instruction " %%cl, %" format "[v]; " \
                     "pushfq; popq %[after]" \
        : [v] "+q"(value),[after] "=r"(after) \
        : [before] "r"(before),"c"((uint8_t)count) : "cc","memory")
static uint16_t native(unsigned op,unsigned wide,unsigned count,
                       uint16_t value,unsigned long before,unsigned long *flags) {
    unsigned long after;uint8_t b=value;uint16_t w=value;
#define ONE(n,ins) case n:if (wide) {NATIVE(ins "w","w",w);} \
                             else {NATIVE(ins "b","b",b);} break
    switch (op) {
    ONE(0,"rol");ONE(1,"ror");ONE(2,"rcl");ONE(3,"rcr");
    ONE(4,"shl");ONE(5,"shr");ONE(6,"shl");ONE(7,"sar");
    default:assert(0);after=0;
    }
#undef ONE
    *flags=after;return wide?w:b;
}
#endif
int main(void) {
    struct real286 cpu;uint8_t data[6]={0};
    real286_reset(&cpu);
    assert(cpu.msw==0xfff0 && cpu.idt_limit==0x3ff && cpu.idt_base==0);
    assert(real286_flags(0xffff)==0xfd7);
    assert(!real286_system(&cpu,1,4,1,data) && data[0]==0xf0 && data[1]==255);
    data[0]=14;
    assert(!real286_system(&cpu,1,6,1,data) && cpu.msw==0xfffe);
    assert(!real286_system(&cpu,6,0,0,data) && cpu.msw==0xfff6);
    data[0]=1;
    assert(real286_system(&cpu,1,6,1,data)==REAL286_PROTECTED && cpu.msw==0xfff6);
    uint8_t table[6]={0x34,0x12,0x56,0x78,0x9a,0x55};
    assert(!real286_system(&cpu,1,2,0,table));
    assert(!real286_system(&cpu,1,0,0,data));
    assert(!memcmp(table,data,5) && data[5]==255);
    assert(real286_system(&cpu,1,0,1,data)==REAL286_UD);
    assert(real286_system(&cpu,0,0,1,data)==REAL286_UD);
    assert(real286_system(&cpu,2,0,1,data)==REAL286_UD);
    unsigned checked=0;
    for (unsigned wide=0;wide<2;wide++)
        for (unsigned op=0;op<8;op++)
            for (unsigned count=0;count<256;count++)
                for (unsigned seed=0;seed<32;seed++) {
                    uint16_t value=(seed*2137u)^(seed*17u);
                    uint16_t flags=0x802|(seed&1);
                    uint16_t result=real286_shift(value,wide,op,count,&flags);
#if defined(__x86_64__) && defined(__GNUC__)
                    unsigned long f;
                    uint16_t expected=native(op,wide,count,value,0x802|(seed&1),&f);
                    unsigned mask=0x8c5;
                    if ((count&31)!=1 && (count&31)) mask&=~0x800;
                    if ((op==4 || op==5 || op==6) && (count&31)>=(wide?16u:8u))
                        mask&=~1u;
                    assert(result==expected);
                    assert((flags&mask)==(f&mask));
#else
                    if (!(count&31)) {
                        assert(result==(value&(wide?65535:255)));
                        assert(flags==(0x802|(seed&1)));
                    }
#endif
                    ++checked;
                }
    printf("PASS: 286 FLAGS, descriptor/MSW operations, protected-mode refusal, %u shift cases\n",checked);
}
