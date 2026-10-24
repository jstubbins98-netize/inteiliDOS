/* inteiliDOS x87 interpreter, MPL-2.0.
 *
 * Intentionally independent of tiny386's upstream host-double FPU.
 * SoftFloat's memory-form API supplies 80-bit arithmetic with integer code.
 * Guest memory is accessed ONLY through the CPU's checked segment/paging API.
 * One CPU instruction is a state transaction: faults/unmasked exceptions do
 * not pop/overwrite registers. Stores check exceptions before touching RAM.
 */
#include "fpu.h"
#include "../softfloat/platform.h"
#include "../softfloat/include/softfloat.h"
#include "../core486_port.h"

typedef extFloat80_t X;
struct FPU {
    X st[8];
    uint16_t cw, sw, tw, cs, ds, opcode;
    uint32_t ip, dp;
    unsigned top;
};
enum { IE=1, DE=2, ZE=4, OE=8, UE=16, PE=32, SF=64, ES=128,
       C0=256, C1=512, C2=1024, C3=16384, BUSY=32768 };
static const X zero={0,0}, one={UINT64_C(0x8000000000000000),0x3fff};
static const X indefinite={UINT64_C(0xc000000000000000),0xffff};

static unsigned slot(FPU *f,int i) {return (f->top+i)&7;}
static unsigned tag(FPU *f,int i) {return (f->tw>>(2*slot(f,i)))&3;}
static void tagset(FPU *f,int i,unsigned t) {
    unsigned n=2*slot(f,i);
    f->tw=(f->tw&~(3u<<n))|(t<<n);
}
static unsigned classify(X x) {
    unsigned e=x.signExp&0x7fff;
    return !e && !x.signif?1:(!e || e==0x7fff ||
                            !(x.signif>>63))?2:0;
}
static bool is_nan(X x) {
    return (x.signExp&0x7fff)==0x7fff &&
           x.signif!=UINT64_C(0x8000000000000000);
}
static bool unsupported(X x) {
    return (x.signExp&0x7fff) && !(x.signif>>63);
}
static void fault(FPU *f,unsigned flags) {f->sw|=flags;}
static X get(FPU *f,int i) {
    if (tag(f,i)==3) {
        fault(f,IE|SF);f->sw&=~C1;return indefinite;
    }
    X x=f->st[slot(f,i)];
    if (unsupported(x)) {fault(f,IE);return indefinite;}
    if (!(x.signExp&0x7fff) && x.signif) fault(f,DE);
    return x;
}
static void set(FPU *f,int i,X x) {
    f->st[slot(f,i)]=x;tagset(f,i,classify(x));
}
static void pop(FPU *f) {tagset(f,0,3);f->top=(f->top+1)&7;}
static void push(FPU *f,X x) {
    f->top=(f->top-1)&7;
    if (tag(f,0)!=3) {fault(f,IE|SF|C1);x=indefinite;}
    set(f,0,x);
}
static uint16_t status(FPU *f) {return (f->sw&~0x3800)|(f->top<<11);}
static void summary(FPU *f) {
    if ((f->sw&~f->cw)&63) f->sw|=ES|BUSY;
    else f->sw&=~(ES|BUSY);
}
bool fpu_pending(FPU *f) {return f && (f->sw&ES);}
void fpu_reset(FPU *f) {
    memset(f,0,sizeof(*f));f->cw=0x37f;f->tw=0xffff;
}
FPU *fpu_new(void) {
    FPU *f=malloc(sizeof(*f));if (f) fpu_reset(f);return f;
}
void fpu_delete(FPU *f) {free(f);}
bool fpu_nowait(int op,int group,int reg,bool memory) {
    return memory ? ((op==1 && (group==6 || group==7)) ||
                     (op==5 && (group==6 || group==7))) :
        ((op==3 && group==4 && reg<=4) ||
         (op==7 && group==4 && reg==0));
}
void fpu_record(FPU *f,uint32_t ip,uint16_t cs,uint16_t opcode,
                uint32_t data,uint16_t ds,bool memory) {
    f->ip=ip;f->cs=cs;f->opcode=opcode&0x7ff;
    if (memory) {f->dp=data;f->ds=ds;}
}
static void begin(FPU *f) {
    static const uint8_t rounding[]={softfloat_round_near_even,
        softfloat_round_min,softfloat_round_max,softfloat_round_minMag};
    softfloat_roundingMode=rounding[(f->cw>>10)&3];
    unsigned pc=(f->cw>>8)&3;
    extF80_roundingPrecision=pc==0?32:pc==2?64:80;
    softfloat_exceptionFlags=0;
}
static bool raised(FPU *f) {
    unsigned e=softfloat_exceptionFlags;
    /* SoftFloat 3 flags are NOT laid out like the x87 status word. */
    if (e&softfloat_flag_invalid) f->sw|=IE;
    if (e&softfloat_flag_infinite) f->sw|=ZE;
    if (e&softfloat_flag_overflow) f->sw|=OE;
    if (e&softfloat_flag_underflow) f->sw|=UE;
    if (e&softfloat_flag_inexact) f->sw|=PE;
    summary(f);return fpu_pending(f);
}
static bool finish(FPU *f,FPU *before,bool ok) {
    if (!ok) {*f=*before;return false;}
    if (raised(f) && ((f->sw&~f->cw)&(IE|DE|ZE))) {
        /* Preserve status/exception pointers, not a speculative result/pop. */
        uint16_t sw=f->sw;
        memcpy(f->st,before->st,sizeof(f->st));
        f->top=before->top;f->tw=before->tw;f->sw=sw;
    }
    return true;
}
static bool ud(CPUI386 *cpu) {cpu_setexc(cpu,6,0);return false;}
static X integer(int64_t v) {X x;i64_to_extF80M(v,&x);return x;}
static X add(X a,X b) {X r;extF80M_add(&a,&b,&r);return r;}
static X sub(X a,X b) {X r;extF80M_sub(&a,&b,&r);return r;}
static X mul(X a,X b) {X r;extF80M_mul(&a,&b,&r);return r;}
static X divide(X a,X b) {X r;extF80M_div(&a,&b,&r);return r;}
static X neg(X a) {a.signExp^=0x8000;return a;}
static bool lt(X a,X b) {return extF80M_lt(&a,&b);}
static bool eq(X a,X b) {return extF80M_eq(&a,&b);}
static void compare(FPU *f,X a,X b,bool quiet) {
    f->sw&=~(C0|C2|C3);
    if (is_nan(a) || is_nan(b)) {
        f->sw|=C0|C2|C3;
        if (!quiet || extF80M_isSignalingNaN(&a) ||
                      extF80M_isSignalingNaN(&b)) fault(f,IE);
    } else if (eq(a,b)) f->sw|=C3;
    else if (lt(a,b)) f->sw|=C0;
}
static X calculate(int group,X a,X b) {
    switch (group) {
    case 0:return add(a,b);
    case 1:return mul(a,b);
    case 4:return sub(a,b);
    case 5:return sub(b,a);
    case 6:return divide(a,b);
    default:return divide(b,a);
    }
}
static X scale(X,int);
static void arithmetic(FPU *f,int group,int dst,X a,X b) {
    if (group==2 || group==3) {compare(f,a,b,false);return;}
    X r=calculate(group,a,b);
    unsigned flags=softfloat_exceptionFlags;
    /* Unmasked arithmetic O/U traps deliver an exponent-wrapped register
     * result, unlike invalid/divide-by-zero which suppress the operation. */
    int wrap=(flags&softfloat_flag_overflow) && !(f->cw&OE)?-24576:
             (flags&softfloat_flag_underflow) && !(f->cw&UE)?24576:0;
    if (wrap) {
        if (group==5 || group==7) {X t=a;a=b;b=t;group^=1;}
        if (group==0 || group==4) {
            a=scale(a,wrap);b=scale(b,wrap);
        } else if (group==1) {
            a=scale(a,wrap/2);b=scale(b,wrap/2);
        } else if (group==6) {
            int e=a.signExp&0x7fff;
            if (e+wrap<=0 || e+wrap>=0x7fff) b=scale(b,-wrap);
            else a=scale(a,wrap);
        }
        r=calculate(group,a,b);
        softfloat_exceptionFlags=flags;
    }
    if ((flags&softfloat_flag_inexact) && (r.signExp&0x7fff)!=0x7fff) {
        unsigned rm=softfloat_roundingMode;
        softfloat_roundingMode=softfloat_round_minMag;
        X truncated=calculate(group,a,b);
        softfloat_roundingMode=rm;softfloat_exceptionFlags=flags;
        if ((r.signExp&0x7fff)>(truncated.signExp&0x7fff) ||
            ((r.signExp&0x7fff)==(truncated.signExp&0x7fff) &&
             r.signif>truncated.signif)) f->sw|=C1;
    }
    set(f,dst,r);
}
/* Accessors intentionally avoid pointer casts: extF80 memory is ten bytes,
 * whereas the compiler's structure is padded to twelve/sixteen bytes. */
static bool read80(CPUI386 *c,int s,uword a,X *x) {
    u32 lo,hi;u16 se;
    if (!cpu_load32(c,s,a,&lo) || !cpu_load32(c,s,a+4,&hi) ||
        !cpu_load16(c,s,a+8,&se)) return false;
    x->signif=((uint64_t)hi<<32)|lo;x->signExp=se;return true;
}
static bool write80(CPUI386 *c,int s,uword a,X x) {
    return cpu_store32(c,s,a,(u32)x.signif) &&
           cpu_store32(c,s,a+4,(u32)(x.signif>>32)) &&
           cpu_store16(c,s,a+8,x.signExp);
}
static bool read64(CPUI386 *c,int s,uword a,uint64_t *v) {
    u32 lo,hi;
    if (!cpu_load32(c,s,a,&lo) || !cpu_load32(c,s,a+4,&hi)) return false;
    *v=((uint64_t)hi<<32)|lo;return true;
}
static bool write64(CPUI386 *c,int s,uword a,uint64_t v) {
    return cpu_store32(c,s,a,(u32)v) && cpu_store32(c,s,a+4,(u32)(v>>32));
}
static bool real_load(FPU *f,CPUI386 *c,int s,uword a,int size,X *x) {
    if (size==4) {
        u32 v;if (!cpu_load32(c,s,a,&v)) return false;
        if (!(v&0x7f800000) && (v&0x7fffff)) fault(f,DE);
        f32_to_extF80M((float32_t){v},x);
    } else {
        uint64_t v;if (!read64(c,s,a,&v)) return false;
        if (!(v&UINT64_C(0x7ff0000000000000)) &&
             (v&UINT64_C(0xfffffffffffff))) fault(f,DE);
        f64_to_extF80M((float64_t){v},x);
    }
    return true;
}
static bool int_load(CPUI386 *c,int s,uword a,int size,X *x) {
    if (size==2) {u16 v;if (!cpu_load16(c,s,a,&v)) return false;*x=integer((s16)v);}
    else if (size==4) {u32 v;if (!cpu_load32(c,s,a,&v)) return false;*x=integer((s32)v);}
    else {uint64_t v;if (!read64(c,s,a,&v)) return false;*x=integer((int64_t)v);}
    return true;
}
/* Environment formats: the 486 real-mode packed pointers differ from the
 * protected-mode selector/offset forms. Sizes follow operand size, not mode. */
static bool environment(FPU *f,CPUI386 *c,int s,uword a,bool shortenv,
                        bool load) {
    CPUI386_State state;cpui386_get_state(c,&state);
    bool real=!(state.cr0&1) || (state.flags&0x20000);
    uint32_t words[7]={f->cw,status(f),f->tw,f->ip,f->cs,f->dp,f->ds};
    unsigned stride=shortenv?2:4;
    if (!load && real) {
        uint32_t ip=(f->cs*16u+f->ip)&0xfffff,dp=(f->ds*16u+f->dp)&0xfffff;
        words[3]=ip&0xffff;words[5]=dp&0xffff;
        words[4]=((ip&0xf0000)>>4)|f->opcode;
        words[6]=(dp&0xf0000)>>4;
        if (!shortenv) {
            words[4]=((ip&0xffff0000)<<12)|f->opcode;
            words[6]=(dp&0xffff0000)<<12;
        }
    } else if (!load && !shortenv) words[4]|=(uint32_t)f->opcode<<16;
    for (unsigned i=0;i<7;i++) {
        if (load) {
            if (shortenv) {
                u16 v;if (!cpu_load16(c,s,a+i*stride,&v)) return false;words[i]=v;
            } else {
                u32 v;if (!cpu_load32(c,s,a+i*stride,&v)) return false;words[i]=v;
            }
        } else if (shortenv) {
            if (!cpu_store16(c,s,a+i*stride,(u16)words[i])) return false;
        } else {
            uint32_t v=words[i];
            if (i<3) v|=0xffff0000u;
            if (!cpu_store32(c,s,a+i*stride,v)) return false;
        }
    }
    if (load) {
        f->cw=words[0];f->sw=words[1]&~0x3800;f->tw=words[2];
        f->top=(words[1]>>11)&7;
        if (real) {
            f->ip=(words[3]&0xffff)|((words[4]<<(shortenv?4:0))&
                    (shortenv?0xf0000:0)); /* high nibble unpacked below */
            f->dp=words[5]&0xffff;
            if (!shortenv) {
                f->ip|=(words[4]>>12)&0xf0000;
                f->dp|=(words[6]>>12)&0xf0000;
            } else f->dp|=(words[6]<<4)&0xf0000;
            f->cs=f->ds=0;f->opcode=words[4]&0x7ff;
        } else {
            f->ip=words[3];f->cs=words[4];f->dp=words[5];f->ds=words[6];
            if (!shortenv) f->opcode=(words[4]>>16)&0x7ff;
        }
        summary(f);
    }
    return true;
}

/* Scaling uses exponent adjustment through exact powers of two. Bounded even
 * for huge/invalid guest arguments; SoftFloat handles overflow/underflow. */
static X scale(X x,int n) {
    if (n>32768) n=32768;
    if (n< -32768) n=-32768;
    while (n>0) {
        int k=n>16000?16000:n;X p={one.signif,(uint16_t)(0x3fff+k)};
        x=mul(x,p);n-=k;
    }
    while (n<0) {
        int k=n< -16000?-16000:n;X p={one.signif,(uint16_t)(0x3fff+k)};
        x=mul(x,p);n-=k;
    }
    return x;
}
/* The transcendental functions are software approximations, not a
 * microcode-accurate reproduction of Intel's transcendental rounding. */
static const X pi={UINT64_C(0xc90fdaa22168c235),0x4000};
static const X ln2={UINT64_C(0xb17217f7d1cf79ac),0x3ffe};
static X logarithm(X x) {
    if (is_nan(x)) return add(x,zero);
    if (!x.signif) {softfloat_exceptionFlags|=softfloat_flag_infinite;return (X){one.signif,0xffff};}
    if (x.signExp&0x8000) {softfloat_exceptionFlags|=softfloat_flag_invalid;return indefinite;}
    if ((x.signExp&0x7fff)==0x7fff) return x;
    int exponent=(x.signExp&0x7fff)-0x3fff;
    if (!(x.signExp&0x7fff)) {
        exponent=1-0x3fff;
        while (!(x.signif>>63)) {x.signif<<=1;--exponent;}
    }
    x.signExp=0x3fff;
    X z=divide(sub(x,one),add(x,one)),z2=mul(z,z),term=z,sum=z;
    for (int i=3;i<=99;i+=2) {term=mul(term,z2);sum=add(sum,divide(term,integer(i)));}
    return add(integer(exponent),divide(add(sum,sum),ln2));
}
static X exp2m1(X x) {
    X y=mul(x,ln2),term=y,sum=y;
    for (int i=2;i<=32;i++) {term=divide(mul(term,y),integer(i));sum=add(sum,term);}
    return sum;
}
static X arctan(X x) {
    bool sign=x.signExp&0x8000;x.signExp&=0x7fff;
    bool inverse=lt(one,x);
    if (inverse) x=divide(one,x);
    /* atan(x)=2*atan(x/(1+sqrt(1+x*x))) gives a rapidly convergent series. */
    X root;X t=add(one,mul(x,x));extF80M_sqrt(&t,&root);
    x=divide(x,add(one,root));
    X term=x,sum=x,x2=neg(mul(x,x));
    for (int i=3;i<=79;i+=2) {term=mul(term,x2);sum=add(sum,divide(term,integer(i)));}
    sum=add(sum,sum);
    if (inverse) sum=sub(scale(pi,-1),sum);
    return sign?neg(sum):sum;
}
static int normalized(X *x) {
    int e=x->signExp&0x7fff;
    if (!e) {
        e=1;
        while (x->signif && !(x->signif>>63)) {x->signif<<=1;--e;}
    }
    return e;
}
static void fp_remainder(FPU *f,X a,X b,bool nearest) {
    X r,na=a,nb=b;
    int ea=normalized(&na),eb=normalized(&nb),diff=ea-eb;
    unsigned precision=extF80_roundingPrecision;
    extF80_roundingPrecision=80; /* FPREM ignores precision control. */
    bool partial=diff>=64 && (a.signExp&0x7fff)!=0x7fff &&
        (b.signExp&0x7fff)!=0x7fff && a.signif && b.signif;
    X divisor=b;
    if (partial) {
        divisor=nb;divisor.signExp=(b.signExp&0x8000)|(ea-32);
    }
    extF80M_rem(&a,&divisor,&r);
    if (!nearest && r.signif && ((r.signExp^a.signExp)&0x8000) &&
        !is_nan(r)) {
        divisor.signExp=(divisor.signExp&0x7fff)|(a.signExp&0x8000);
        r=add(r,divisor);
    }
    extF80_roundingPrecision=precision;set(f,0,r);
    if (partial) {f->sw|=C2;return;}
    f->sw&=~(C0|C1|C2|C3);
    if (is_nan(r) || (b.signExp&0x7fff)==0x7fff || !b.signif) return;
    /* Quotient low bits without a signed-64 conversion (which would raise
     * spurious invalid exceptions for legitimate very large quotients). */
    unsigned q=0;uint64_t rem=na.signif,d=nb.signif;
    if (diff>=0) {
        if (rem>=d) {rem-=d;q=1;}
        for (int i=0;i<diff;i++) {
            bool carry=rem>>63;rem<<=1;q=(q<<1)&7;
            if (carry || rem>=d) {rem-=d;q|=1;}
        }
        if (nearest && ((rem>>63) || (rem<<1)>d ||
                        ((rem<<1)==d && (q&1)))) q=(q+1)&7;
    } else if (nearest && diff==-1 && na.signif>nb.signif) q=1;
    if (q&4) f->sw|=C0;
    if (q&2) f->sw|=C3;
    if (q&1) f->sw|=C1;
}
static bool trig(FPU *f,X x,X *sinval,X *cosval) {
    if ((x.signExp&0x7fff)>=0x403e && !is_nan(x)) {
        if ((x.signExp&0x7fff)==0x7fff) {fault(f,IE);*sinval=*cosval=indefinite;return true;}
        f->sw|=C2;return false;
    }
    f->sw&=~C2;
    if (is_nan(x)) {*sinval=*cosval=add(x,zero);return true;}
    X period=scale(pi,1),r;extF80M_rem(&x,&period,&r);
    X r2=neg(mul(r,r)),st=r,ct=one,ss=r,cs=one;
    for (int i=1;i<=24;i++) {
        st=divide(mul(st,r2),integer((2*i)*(2*i+1)));ss=add(ss,st);
        ct=divide(mul(ct,r2),integer((2*i-1)*(2*i)));cs=add(cs,ct);
    }
    *sinval=ss;*cosval=cs;return true;
}
static bool special(FPU *f,int reg) {
    X a=get(f,0),b,r,t;
    switch (reg) {
    case 0: /* F2XM1; the specified domain is [-1,1]. */
        set(f,0,exp2m1(a));break;
    case 1: b=get(f,1);set(f,1,mul(b,logarithm(a)));pop(f);break;
    case 2:
        if (trig(f,a,&r,&t)) {set(f,0,divide(r,t));push(f,one);}break;
    case 3: /* FPATAN: atan2(ST1,ST0), preserving quadrants. */
        b=get(f,1);
        if (is_nan(a) || is_nan(b)) r=add(a,b);
        else {
            bool ai=(a.signExp&0x7fff)==0x7fff;
            bool bi=(b.signExp&0x7fff)==0x7fff;
            if (!b.signif || (ai && !bi)) r=(a.signExp&0x8000)?pi:zero;
            else if (!a.signif || bi) {
                r=scale(pi,-1);
                if (ai && bi) r=mul(pi,divide(integer((a.signExp&0x8000)?3:1),integer(4)));
            } else {
                r=arctan(divide(b,a));
                if (a.signExp&0x8000) r=add(r,(b.signExp&0x8000)?neg(pi):pi);
            }
            r.signExp=(r.signExp&0x7fff)|(b.signExp&0x8000);
        }
        set(f,1,r);pop(f);break;
    case 4: /* FXTRACT */
        if (!a.signif) {fault(f,ZE);set(f,0,(X){one.signif,0xffff});push(f,a);}
        else if ((a.signExp&0x7fff)==0x7fff) {
            set(f,0,is_nan(a)?add(a,zero):(X){one.signif,0x7fff});push(f,a);
        } else {
            int e=(a.signExp&0x7fff)-0x3fff;
            if (!(a.signExp&0x7fff)) {
                e=1-0x3fff;while (!(a.signif>>63)) {a.signif<<=1;--e;}
            }
            a.signExp=(a.signExp&0x8000)|0x3fff;set(f,0,integer(e));push(f,a);
        }break;
    case 5:case 8: /* FPREM1 / FPREM, quotient bits and partial reduction. */
        fp_remainder(f,a,get(f,1),reg==5);break;
    case 9:b=get(f,1);set(f,1,mul(b,logarithm(add(one,a))));pop(f);break;
    case 10:extF80M_sqrt(&a,&r);set(f,0,r);break;
    case 11:if (trig(f,a,&r,&t)) {set(f,0,r);push(f,t);}break;
    case 12:extF80M_roundToInt(&a,softfloat_roundingMode,true,&r);set(f,0,r);break;
    case 13:
        b=get(f,1);
        if (is_nan(b)) set(f,0,add(a,b));
        else {
            int n;
            if ((b.signExp&0x7fff)>=0x400e) n=(b.signExp&0x8000)?-32768:32768;
            else n=(int)extF80M_to_i32(&b,softfloat_round_minMag,false);
            set(f,0,scale(a,n));
        }break;
    case 14:if (trig(f,a,&r,&t)) set(f,0,r);break;
    case 15:if (trig(f,a,&r,&t)) set(f,0,t);break;
    default:return false;
    }
    return true;
}
static bool register_op(FPU *f,CPUI386 *cpu,int op,int group,int reg) {
    if (op==0) {
        arithmetic(f,group,0,get(f,0),get(f,reg));if (group==3) pop(f);
    } else if (op==4 || op==6) {
        if (op==6 && group==3 && reg==1) {
            compare(f,get(f,0),get(f,1),false);pop(f);pop(f);
        } else {
            if (group==2 || group==3) return ud(cpu);
            /* DC/DE encode subtract/divide in the opposite order to D8. */
            int g=group>=4?group^1:group;
            arithmetic(f,g,reg,get(f,reg),get(f,0));if (op==6) pop(f);
        }
    } else if (op==1) {
        switch (group) {
        case 0:push(f,get(f,reg));break;
        case 1:{X a=get(f,0),b=get(f,reg);set(f,0,b);set(f,reg,a);break;}
        case 2:if (reg) return ud(cpu);break; /* FNOP */
        case 4:
            if (reg==0) set(f,0,neg(get(f,0)));
            else if (reg==1) {X x=get(f,0);x.signExp&=0x7fff;set(f,0,x);}
            else if (reg==4) compare(f,get(f,0),zero,false);
            else if (reg==5) {
                unsigned tt=tag(f,0);X x=f->st[slot(f,0)];
                f->sw&=~(C0|C1|C2|C3);
                if (tt==3) f->sw|=C0|C3;
                else {
                    if (x.signExp&0x8000) f->sw|=C1;
                    unsigned e=x.signExp&0x7fff;
                    if (!x.signif && !e) f->sw|=C3;
                    else if (is_nan(x)) f->sw|=C0;
                    else if (e==0x7fff) f->sw|=C0|C2;
                    else if (!e) f->sw|=C2|C3;
                    else if (!unsupported(x)) f->sw|=C2;
                }
            } else return ud(cpu);
            break;
        case 5:{
            static const X constants[]={
                {UINT64_C(0x8000000000000000),0x3fff},
                {UINT64_C(0xd49a784bcd1b8afe),0x4000},
                {UINT64_C(0xb8aa3b295c17f0bc),0x3fff},
                {UINT64_C(0xc90fdaa22168c235),0x4000},
                {UINT64_C(0x9a209a84fbcff799),0x3ffd},
                {UINT64_C(0xb17217f7d1cf79ac),0x3ffe},{0,0}};
            if (reg>6) return ud(cpu);
            X x=constants[reg];
            /* Intel constant loads honor RC without setting precision. */
            unsigned rc=(f->cw>>10)&3;
            if ((reg==1 || reg==2 || reg==4) && rc==2) ++x.signif;
            if ((reg==3 || reg==5) && (rc==1 || rc==3)) --x.signif;
            push(f,x);break;
        }
        case 6:
            if (reg==6 || reg==7) {f->top=(f->top+(reg==6?7:1))&7;}
            else return special(f,reg);
            break;
        case 7:return special(f,reg+8);
        default:return ud(cpu);
        }
    } else if (op==2) {
        if (group!=5 || reg!=1) return ud(cpu);
        compare(f,get(f,0),get(f,1),true);pop(f);pop(f);
    } else if (op==3) {
        if (group!=4 || reg>4) return ud(cpu);
        if (reg==2) {f->sw&=~(BUSY|ES|SF|63);}
        else if (reg==3) fpu_reset(f);
        /* FENI/FDISI/FSETPM are 486 no-ops. */
    } else if (op==5) {
        if (group==0) tagset(f,reg,3);
        else if (group==2 || group==3) {set(f,reg,get(f,0));if (group==3) pop(f);}
        else if (group==4 || group==5) {compare(f,get(f,0),get(f,reg),true);if (group==5) pop(f);}
        else return ud(cpu);
    } else if (op==7 && group==4 && reg==0) cpu_setax(cpu,status(f));
    else return ud(cpu);
    return true;
}
bool fpu_exec1(FPU *f,CPUI386 *cpu,int op,int group,int reg) {
    FPU old=*f;begin(f);
    if (!fpu_nowait(op,group,reg,false)) f->sw&=~C1;
    return finish(f,&old,register_op(f,cpu,op,group,reg));
}
static bool memory_op(FPU *f,CPUI386 *c,bool os16,int op,int group,int s,uword a) {
    X x;uint64_t bits;u16 v;
    if (op==0 || op==4 || op==2 || op==6) {
        bool ok=(op&2)?int_load(c,s,a,op==2?4:2,&x):
                           real_load(f,c,s,a,op==0?4:8,&x);
        if (!ok) return false;
        arithmetic(f,group,0,get(f,0),x);if (group==3) pop(f);return true;
    }
    if (op==1 && group>=4) {
        if (group==4) return environment(f,c,s,a,os16,true);
        if (group==5) {
            if (!cpu_load16(c,s,a,&v)) return false;
            f->cw=v;summary(f);return true;
        }
        if (group==6) {
            if (!environment(f,c,s,a,os16,false)) return false;
            f->cw|=63;summary(f);return true;
        }
        return cpu_store16(c,s,a,f->cw);
    }
    if (op==5 && (group==4 || group==6)) {
        bool load=group==4;
        if (!environment(f,c,s,a,os16,load)) return false;
        unsigned start=os16?14:28;
        for (int i=0;i<8;i++) {
            if (load) {
                if (!read80(c,s,a+start+10*i,&x)) return false;
                f->st[slot(f,i)]=x;
            } else if (!write80(c,s,a+start+10*i,f->st[slot(f,i)])) return false;
        }
        if (!load) fpu_reset(f);
        return true;
    }
    if (op==5 && group==7) return cpu_store16(c,s,a,status(f));
    if (group==0) {
        if (op==1 || op==5) {
            if (!real_load(f,c,s,a,op==1?4:8,&x)) return false;
        } else if (!int_load(c,s,a,op==3?4:2,&x)) return false;
        push(f,x);return true;
    }
    if (op==3 && group==5) {
        if (!read80(c,s,a,&x)) return false;
        /* FLD m80 copies unsupported/NaN/denormal encodings verbatim. */
        push(f,x);return true;
    }
    if (op==7 && group==5) {
        if (!int_load(c,s,a,8,&x)) return false;
        push(f,x);return true;
    }
    if (op==7 && group==4) { /* FBLD packed 18-digit decimal */
        uint64_t n=0;uint8_t digits[10];bool valid=true;
        for (int i=0;i<10;i++) if (!cpu_load8(c,s,a+i,&digits[i])) return false;
        for (int i=8;i>=0;i--) {
            unsigned hi=digits[i]>>4,lo=digits[i]&15;
            if (hi>9 || lo>9) valid=false;
            n=n*100+hi*10+lo;
        }
        if (!valid) {fault(f,IE);x=indefinite;}
        else {x=integer((int64_t)n);if (digits[9]&128) x.signExp|=0x8000;}
        push(f,x);return true;
    }
    bool real=(op==1 || op==5);
    bool store=(group==2 || group==3) ||
        (op==3 && group==7) || (op==7 && (group==6 || group==7));
    if (!store) return ud(c);
    x=get(f,0);
    if (op==3 && group==7) { /* FSTP m80 */
        if (raised(f)) return true;
        if (!write80(c,s,a,x)) return false;
    } else if (real) {
        if (op==1) bits=extF80M_to_f32(&x).v;
        else bits=extF80M_to_f64(&x).v;
        if (raised(f)) return true;
        if (op==1) {if (!cpu_store32(c,s,a,(u32)bits)) return false;}
        else if (!write64(c,s,a,bits)) return false;
    } else {
        int64_t n=extF80M_to_i64(&x,softfloat_roundingMode,true);
        int size=op==3?4:group==7?8:2;
        if (op==7 && group==6) size=10;
        if (size==2 && (n< -32768 || n>32767)) fault(f,IE);
        if (size==4 && (n<INT32_MIN || n>INT32_MAX)) fault(f,IE);
        if (size==10 && (n<=-INT64_C(1000000000000000000) ||
                        n>=INT64_C(1000000000000000000))) fault(f,IE);
        if (raised(f)) return true;
        bool invalid=(f->sw&IE) && ((softfloat_exceptionFlags&softfloat_flag_invalid) ||
            (size==2 && (n< -32768 || n>32767)) ||
            (size==4 && (n<INT32_MIN || n>INT32_MAX)) ||
            (size==10 && (n<=-INT64_C(1000000000000000000) ||
                         n>=INT64_C(1000000000000000000))) || tag(f,0)==3);
        if (size==10) {
            uint64_t mag=n<0?-(uint64_t)n:(uint64_t)n;
            for (int i=0;i<10;i++) {
                unsigned byte;
                if (invalid) byte=i>=7?0xff:0;
                else if (i==9) byte=x.signExp&0x8000?0x80:0;
                else {unsigned lo=mag%10;mag/=10;byte=lo+16*(mag%10);mag/=10;}
                if (!cpu_store8(c,s,a+i,byte)) return false;
            }
        } else if (size==2) {
            if (!cpu_store16(c,s,a,invalid?0x8000:(u16)n)) return false;
        } else if (size==4) {
            if (!cpu_store32(c,s,a,invalid?0x80000000u:(u32)n)) return false;
        } else if (!write64(c,s,a,invalid?UINT64_C(0x8000000000000000):(uint64_t)n)) return false;
    }
    if (group==3 || group==6 || group==7) pop(f);
    return true;
}
bool fpu_exec2(FPU *f,CPUI386 *c,bool os16,int op,int group,int s,uword a) {
    FPU old=*f;begin(f);
    bool control=(op==1 && group>=4) || (op==5 && group>=4);
    if (control) {
        bool ok=memory_op(f,c,os16,op,group,s,a);
        if (!ok) *f=old;
        return ok;
    }
    f->sw&=~C1;
    return finish(f,&old,memory_op(f,c,os16,op,group,s,a));
}
