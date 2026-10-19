/* Intel 80286 real-mode compatibility helpers. MPL-2.0. */
#include "real286.h"

void real286_reset(struct real286 *cpu) {
    cpu->gdt_base=cpu->idt_base=0;
    cpu->gdt_limit=0xffff;cpu->idt_limit=0x3ff;
    cpu->msw=0xfff0;
}
uint16_t real286_flags(unsigned flags) {
    /* Real-mode 286 POPF cannot set IOPL/NT; bit 15 is always zero. */
    return (flags&0x0fd5)|2;
}
int real286_system(struct real286 *cpu,unsigned opcode,unsigned group,
                   int is_register,uint8_t operand[6]) {
    if (opcode==6) {cpu->msw&=~8;return REAL286_OK;} /* CLTS */
    if (opcode!=1 || group==5 || group==7 ||
        (group<4 && is_register)) return REAL286_UD;
    if (group<4) {
        uint16_t *limit=(group&1)?&cpu->idt_limit:&cpu->gdt_limit;
        uint32_t *base=(group&1)?&cpu->idt_base:&cpu->gdt_base;
        if (group<2) {
            operand[0]=*limit;operand[1]=*limit>>8;
            operand[2]=*base;operand[3]=*base>>8;operand[4]=*base>>16;
            operand[5]=0xff; /* 286, unlike the 386's 16-bit form. */
        } else {
            *limit=operand[0]|((uint16_t)operand[1]<<8);
            *base=operand[2]|((uint32_t)operand[3]<<8)|((uint32_t)operand[4]<<16);
        }
    } else if (group==4) { /* SMSW */
        operand[0]=cpu->msw;operand[1]=cpu->msw>>8;
    } else { /* LMSW: reject PE before changing any guest state. */
        if (operand[0]&1) return REAL286_PROTECTED;
        cpu->msw=0xfff0|(operand[0]&14);
    }
    return REAL286_OK;
}
uint16_t real286_shift(uint16_t value,unsigned wide,unsigned operation,
                      unsigned count,uint16_t *flags) {
    unsigned width=wide?16:8,mask=wide?65535:255,top=1u<<(width-1);
    unsigned original=value&mask,result=original,cf=*flags&1;
    count&=31;
    if (!count) return result;
    for (unsigned i=0;i<count;i++) {
        unsigned carry;
        switch (operation) {
        case 0:carry=!!(result&top);result=((result<<1)|carry)&mask;break;
        case 1:carry=result&1;result=(result>>1)|(carry?top:0);break;
        case 2:carry=!!(result&top);result=((result<<1)|cf)&mask;break;
        case 3:carry=result&1;result=(result>>1)|(cf?top:0);break;
        case 4:case 6:carry=!!(result&top);result=(result<<1)&mask;break;
        case 5:carry=result&1;result>>=1;break;
        default:carry=result&1;result=(result>>1)|(result&top);break;
        }
        cf=carry;
    }
    unsigned f=(*flags&~1u)|cf;
    if (count==1) {
        unsigned of;
        if (operation==0 || operation==2 || operation==4 || operation==6)
            of=!!(result&top)^cf;
        else if (operation==1 || operation==3)
            of=!!(result&top)^!!(result&(top>>1));
        else of=operation==5?!!(original&top):0;
        f=(f&~0x800u)|(of<<11);
    }
    if (operation>=4) {
        unsigned parity=result&255;
        parity^=parity>>4;parity^=parity>>2;parity^=parity>>1;
        f=(f&~0xc4u)|(!result?0x40:0)|(result&top?0x80:0)|(!(parity&1)?4:0);
    }
    *flags=real286_flags(f);
    return result;
}
