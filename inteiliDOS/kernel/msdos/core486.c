/* Isolated 486SX/DX machine adapter. MPL-2.0; CPU core is BSD-3-Clause. */
#include "core486.h"
#include "tiny386/i386.h"
#include "msdos.h"
#include "video.h"
#include "mouse.h"
#include "sound.h"
#include "bios_keyboard.h"
#include "keyboard_input.h"
#include "../memory.h"

enum {AX,CX,DX,BX,SP,BP,SI,DI,ES,CS,SS,DS};
static uint8_t *ram,*ports;
static CPUI386 *cpu;
static int pending_irq,a20,kbc_write,kbc_read,posted;
static uint8_t pic_base,pic_mask,pic_init,pic_icw1;
static uint8_t pic_service,pic_read_isr;
static int pending_line;
static int key_break;
static uint16_t mouse_regs[12],mouse_ip,mouse_flags;
static uint32_t mouse_high[8];
static int mouse_busy;
/* Only the virtual slave PIC's FPU line is needed here. Never touch the
 * native PIC or host FPU when delivering a legacy DOS coprocessor error. */
static uint8_t slave_base,slave_mask,slave_init,slave_icw1,slave_service,slave_read_isr;
static int ferr,ferr_acked;
static void fpu_error(void *unused,int asserted) {
    (void)unused;ferr=asserted;if (!asserted) ferr_acked=0;
}

static unsigned word(unsigned p) { return ram[p]|((unsigned)ram[p+1]<<8); }
static void putword(unsigned p,unsigned v) {ram[p]=v;ram[p+1]=v>>8;}
static void putdword(unsigned p,uint32_t v) {putword(p,v);putword(p+2,v>>16);}
static void gate(int enabled) {
    a20=!!enabled;cpui386_set_a20(cpu,a20);ports[0x92]=(uint8_t)(a20<<1);
}
static int irq(void *opaque) {
    (void)opaque;int n=pending_irq;pending_irq=-1;
    if (pending_line>=0) {
        CPUI386_State state;cpui386_get_state(cpu,&state);
        /* CLI can leave an IRQ pending across a mode switch/PIC remap.
         * Select its vector when the CPU accepts it, never when queued. */
        if (pending_line==13) {
            if ((pic_mask&4) || (slave_mask&32)) {n=-1;ferr_acked=0;}
            else {n=slave_base+5;slave_service|=32;pic_service|=4;}
        } else if ((state.cr0&1) || pending_line==5) {
            if (pic_mask&(1u<<pending_line)) n=-1;
            else {n=pic_base+pending_line;pic_service|=1u<<pending_line;}
        } else n=pending_line==0?0xa:7;
    }
    pending_line=-1;return n;
}
static uint8_t read8(void *opaque,int p) {
    (void)opaque;p&=65535;
    if (p==0x92) return ports[p];
    if (p==0x21) return pic_mask;
    if (p==0xa1) return slave_mask;
    if (p==0xa0) return slave_read_isr?slave_service:(ferr && !ferr_acked?32:0);
    if (p==0x20) return pic_read_isr?pic_service:
        (pending_line>=0?(1u<<pending_line):0);
    if (p==0x64 && kbc_read) return 1;
    if (p==0x60 && kbc_read) {kbc_read=0;return 1|(a20<<1);}
    if (p==0x71) {
        unsigned c=ports[0x70]&127,kib=MSDOS_486_RAM/1024-1024;
        if (c==0x17 || c==0x30) return kib;
        if (c==0x18 || c==0x31) return kib>>8;
    }
    return msdos_486_in(p,0);
}
static void write8(void *opaque,int p,uint8_t v) {
    (void)opaque;p&=65535;
    if (p==0xf0 || p==0xf1) {ferr_acked=1;return;}
    if (p==0xa0) {
        if ((v&0xf0)==0x10) {
            slave_init=1;slave_icw1=v;slave_mask=255;slave_service=0;
        } else if ((v&0x18)==8) {
            if (v&2) slave_read_isr=v&1;
        } else if ((v&0xe0)==0x60) slave_service&=~(1u<<(v&7));
        else if ((v&0xe0)==0x20) {
            for (unsigned i=0;i<8;i++) if (slave_service&(1u<<i)) {
                slave_service&=~(1u<<i);break;
            }
        }
        return;
    }
    if (p==0xa1) {
        if (slave_init==1) {
            slave_base=v&0xf8;slave_init=(slave_icw1&2)?((slave_icw1&1)?3:0):2;
        } else if (slave_init==2) slave_init=(slave_icw1&1)?3:0;
        else if (slave_init==3) slave_init=0;
        else slave_mask=v;
        return;
    }
    if (p==0x92) {gate(v&2);return;} /* bit 0 must never reset the host */
    if (p==0x64) {
        if (v==0xd1) kbc_write=1;
        else if (v==0xd0) kbc_read=1;
        else if (v==0xdd || v==0xdf) gate(v==0xdf);
        return;
    }
    if (p==0x60 && kbc_write) {kbc_write=0;gate(v&2);return;}
    /* Protected-mode extenders remap IRQs away from exception vectors.
     * Track the master PIC even with sound disabled. Never deliver the old
     * real-mode BIOS's private timer vector 0Ah as protected-mode #TS. */
    if (p==0x20 && (v&0xf0)==0x10) {
        pic_init=1;pic_icw1=v;pic_mask=255;pic_service=0;
    } else if (p==0x20 && (v&0x18)==8) {
        if (v&2) pic_read_isr=v&1;
    } else if (p==0x20 && (v&0xe0)==0x60) {
        pic_service&=~(1u<<(v&7));
    } else if (p==0x20 && (v&0xe0)==0x20) {
        for (unsigned i=0;i<8;i++) if (pic_service&(1u<<i)) {
            pic_service&=~(1u<<i);break;
        }
    } else if (p==0x21) {
        if (pic_init==1) {
            pic_base=v&0xf8;
            pic_init=(pic_icw1&2)?((pic_icw1&1)?3:0):2;
        } else if (pic_init==2) pic_init=(pic_icw1&1)?3:0;
        else if (pic_init==3) pic_init=0;
        else pic_mask=v;
    }
    msdos_486_out(p,v,0);
}
static uint16_t read16(void *o,int p) {
    return read8(o,p)|((unsigned)read8(o,p+1)<<8);
}
static uint32_t read32(void *o,int p) {
    return read16(o,p)|((uint32_t)read16(o,p+2)<<16);
}
static void write16(void *o,int p,uint16_t v) {write8(o,p,v);write8(o,p+1,v>>8);}
static void write32(void *o,int p,uint32_t v) {write16(o,p,v);write16(o,p+2,v>>16);}
static uint8_t video_read8(void *o,uint32_t p) {
    (void)o;
    if (p>=MSDOS_486_RAM) return 0xff;
    return msdos_video_mapped(p)?msdos_video_read(p):ram[p];
}
static void video_write8(void *o,uint32_t p,uint8_t v) {
    (void)o;
    if (p>=MSDOS_486_RAM) return;
    if (msdos_video_mapped(p)) msdos_video_write(p,v);else ram[p]=v;
}
static uint16_t video_read16(void *o,uint32_t p) {
    return video_read8(o,p)|((unsigned)video_read8(o,p+1)<<8);
}
static uint32_t video_read32(void *o,uint32_t p) {
    return video_read16(o,p)|((uint32_t)video_read16(o,p+2)<<16);
}
static void video_write16(void *o,uint32_t p,uint16_t v) {
    video_write8(o,p,v);video_write8(o,p+1,v>>8);
}
static void video_write32(void *o,uint32_t p,uint32_t v) {
    video_write16(o,p,v);video_write16(o,p+2,v>>16);
}
/* BIOS INT 15h: private guest RAM and A20 only, never host BIOS calls. */
static void memory_bios(uint16_t r[12],uint16_t *flags) {
    unsigned ah=r[AX]>>8;
    *flags&=~1u;
    if (ah==0x88) r[AX]=MSDOS_486_RAM/1024-1024;
    else if (r[AX]==0xe801) {
        r[AX]=r[CX]=MSDOS_486_RAM/1024-1024;r[BX]=r[DX]=0;
    } else if (ah==0x24 && (r[AX]&255)<=3) {
        unsigned sub=r[AX]&255;
        if (sub<2) {gate(sub);r[AX]=0;}
        else if (sub==2) {r[AX]=a20;r[CX]=0;}
        else {r[AX]=0;r[BX]=3;}
    } else if (ah==0x87) {
        unsigned table=((unsigned)r[ES]<<4)+r[SI],length=(unsigned)r[CX]*2;
        if (table>MSDOS_486_RAM-32) goto fail;
        unsigned src=word(table+18)|((unsigned)ram[table+20]<<16)|
                     ((unsigned)ram[table+23]<<24);
        unsigned dst=word(table+26)|((unsigned)ram[table+28]<<16)|
                     ((unsigned)ram[table+31]<<24);
        if (src>MSDOS_486_RAM || length>MSDOS_486_RAM-src ||
            dst>MSDOS_486_RAM || length>MSDOS_486_RAM-dst ||
            (length && (word(table+16)+1u<length || word(table+24)+1u<length)))
            goto fail;
        if (dst>src && dst-src<length)
            for (unsigned i=length;i;i--) ram[dst+i-1]=ram[src+i-1];
        else for (unsigned i=0;i<length;i++) ram[dst+i]=ram[src+i];
        r[AX]=0;
    } else {
fail:   r[AX]=(r[AX]&255)|0x8600;*flags|=1;
    }
}
static void timer_bios(void) {
    /* The polling ROM's INT 08h only calls INT 1Ch: its private INT 0Ah
     * already maintains time and never acknowledges a hardware PIC.
     * A DPMI client can instead chain an actual IRQ0 back to this entry.
     * Finish that BIOS tick and EOI here, without double-counting the
     * private real-mode polling path or touching the native PIC. */
    if (!(pic_service&1)) return;
    uint32_t ticks=word(0x46c)|((uint32_t)word(0x46e)<<16);
    if (++ticks>=0x1800b0) {ticks=0;++ram[0x470];}
    putdword(0x46c,ticks);
    pic_service&=~1u;
}
static int firmware(void *opaque,CPUI386 *c) {
    (void)opaque;
    uint16_t r[12],ip,flags;
    if (!cpui386_real_state(c,r,&ip,&flags,0)) return 0;
    unsigned address=(((unsigned)r[CS]<<4)+ip)&0xfffff;
    /* Intercept the checked original entry, including PUSHF/FAR CALL
     * chains from an extender, not just software INT 08h dispatch. */
    if (r[CS]==0xf000 && ip==0x73f && ram[address]==0xcd &&
        ram[address+1]==0x1c && ram[address+2]==0xcf)
        timer_bios();
    if (!posted && address==0x7c00) {
        /* POST initializes the IVT. Install the memory service afterwards,
         * before IO.SYS/HIMEM can query it, including far-call chains. */
        putword(0x15*4,0x3200);putword(0x15*4+2,0xf000);
        ram[0xf3200]=0xcf;posted=1;
        /* BIOS equipment detection must agree with CPUID/FNINIT. Preserve
         * the upstream ROM, using a source-defined RAM overlay as with the
         * memory service. The INT 11h stub reads the live BDA with DS=0. */
        static const uint8_t equipment[]={
            0x1e,0x31,0xc0,0x8e,0xd8,0xa1,0x10,0x04,0x1f,0xcf};
        kmemcpy(ram+0xf3210,equipment,sizeof(equipment));
        putword(0x11*4,0x3210);putword(0x11*4+2,0xf000);
#ifdef I386_ENABLE_FPU
        putword(0x410,word(0x410)|2);
#else
        putword(0x410,word(0x410)&~2u);
#endif
        /* The polling ROM does not initialize an AT PIC. Expose normal BIOS
         * timer/keyboard masks before an extender saves/remaps the PIC. */
        pic_mask=0xfc;
#ifdef I386_ENABLE_FPU
        pic_mask&=~4u; /* AT cascade must pass the slave's x87 IRQ13. */
#endif
        ports[0x21]=pic_mask;
        msdos_486_out(0x21,pic_mask,0);
    }
    if (r[CS]==0xf000 && ip==0x3200) {
        unsigned stack=(((unsigned)r[SS]<<4)+r[SP])&0xfffff;
        if (stack>0xffffa) {return 0;}
        uint16_t saved=word(stack+4);
        CPUI386_State state;cpui386_get_state(c,&state);
        if (r[AX]==0xe820 && state.gpr[DX]==0x534d4150 &&
            state.gpr[CX]>=20 && state.gpr[BX]<3) {
            unsigned n=state.gpr[BX],p=((unsigned)r[ES]<<4)+r[DI];
            if (p<=0x9fc00-20) {
                static const uint32_t bases[]={0,0x9fc00,0x100000};
                static const uint32_t lengths[]={0x9fc00,0x60400,MSDOS_486_RAM-0x100000};
                putdword(p,bases[n]);putdword(p+4,0);
                putdword(p+8,lengths[n]);putdword(p+12,0);
                putdword(p+16,n==1?2:1);
                cpui386_set_gpr(c,AX,0x534d4150);
                cpui386_set_gpr(c,CX,20);cpui386_set_gpr(c,BX,n==2?0:n+1);
                r[AX]=0x4150;r[CX]=20;r[BX]=n==2?0:n+1;saved&=~1u;
            } else {r[AX]=0x8600;saved|=1;}
        } else memory_bios(r,&saved);
        ip=word(stack);r[CS]=word(stack+2);r[SP]+=6;flags=saved;
        cpui386_real_state(c,r,&ip,&flags,1);return 1;
    }
    if (mouse_busy && r[CS]==0xf000 && ip==MSDOS_MOUSE_RETURN) {
        for (unsigned i=0;i<8;i++) cpui386_set_gpr(c,i,mouse_high[i]);
        cpui386_real_state(c,mouse_regs,&mouse_ip,&mouse_flags,1);
        mouse_busy=0;return 1;
    }
    if (msdos_486_bios(r,&ip,&flags)) {
        cpui386_real_state(c,r,&ip,&flags,1);return 1;
    }
    return 0;
}
static void mouse_event(void) {
    uint16_t r[12],ip,flags,segment,offset;
    if (mouse_busy || !cpui386_real_state(cpu,r,&ip,&flags,0) ||
        !(flags&0x200) || (flags&0x100)) return;
    kmemcpy(mouse_regs,r,sizeof(r));mouse_ip=ip;mouse_flags=flags;
    if (!msdos_mouse_event(r,&segment,&offset)) return;
    CPUI386_State state;cpui386_get_state(cpu,&state);
    kmemcpy(mouse_high,state.gpr,sizeof(mouse_high));
    r[SP]-=4;
    unsigned stack=(((unsigned)r[SS]<<4)+r[SP])&0xfffff;
    if (stack>0xffffc) return;
    putword(stack,MSDOS_MOUSE_RETURN);putword(stack+2,0xf000);
    r[CS]=segment;ip=offset;
    cpui386_real_state(cpu,r,&ip,&flags,1);mouse_busy=1;
}
int msdos_486_execute(uint8_t *memory,uint8_t *io) {
    ram=memory;ports=io;CPU_CB *cb;
    cpu=cpui386_new(4,(char *)ram,MSDOS_486_RAM,&cb);
    if (!cpu) return -1;
#ifdef I386_ENABLE_FPU
    if (!cpui386_enable_fpu(cpu)) {cpui386_delete(cpu);cpu=0;return -1;}
#endif
    cb->io=0;cb->before_exec=firmware;
    cb->fpu_error=fpu_error;
    cb->pic_read_irq=irq;
    cb->io_read8=read8;cb->io_read16=read16;cb->io_read32=read32;
    cb->io_write8=write8;cb->io_write16=write16;cb->io_write32=write32;
    cb->iomem_read8=video_read8;cb->iomem_read16=video_read16;cb->iomem_read32=video_read32;
    cb->iomem_write8=video_write8;cb->iomem_write16=video_write16;cb->iomem_write32=video_write32;
    pending_irq=-1;a20=kbc_write=kbc_read=posted=mouse_busy=0;gate(0);
    pic_base=8;pic_mask=pic_init=pic_icw1=pic_service=pic_read_isr=0;
    slave_base=0x70;slave_mask=slave_init=slave_icw1=slave_service=slave_read_isr=0;
    ferr=ferr_acked=0;
    key_break=pending_line=-1;
    cpui386_boot(cpu,(uint16_t *)(ram+0xf0000),0x100);
    int result=0;unsigned rounds=0,timer_pending=0;
    for (;;) {
        cpui386_step(cpu,1000);
        if (cpui386_error(cpu)) {result=-2;break;}
        if (msdos_pc_poll(ram,ports)) break;
        ++rounds;
        if (!(rounds%20)) timer_pending=1;
        if (pending_irq<0) {
            if (ferr && !ferr_acked && !(pic_mask&4) && !(slave_mask&32)) {
                pending_line=13;pending_irq=slave_base+5;ferr_acked=1;
                cpui386_raise_irq(cpu);
                continue;
            }
            CPUI386_State state;cpui386_get_state(cpu,&state);
            int protected_mode=!!(state.cr0&1);
            int key=protected_mode && ((pic_mask&2) || key_break>=0)?-1:msdos_pc_key();
            if (protected_mode && key_break>=0) {
                ports[0x60]=(uint8_t)key_break;ports[0x64]=1;
                pending_irq=pic_base+1;pending_line=1;key_break=-1;
            } else
            if (key>=0) {
                putword(0x4a6,key);putword(0xf0000+MSDOS_KEYBOARD_MAILBOX,
                                               (unsigned)key>>16);
                if (protected_mode) {
                    if (!(key&MSDOS_KEY_STATE_ONLY)) {
                        unsigned scan=(unsigned)key>>24;
                        if (!scan) scan=((unsigned)key>>8)&127;
                        ports[0x60]=scan;ports[0x64]=1;
                        key_break=scan|128;pending_irq=pic_base+1;pending_line=1;
                    }
                } else {pending_irq=7;pending_line=1;}
            } else {
                pending_irq=msdos_sound_irq();
                if (pending_irq>=0) pending_line=5;
                if (pending_irq<0 && timer_pending) {
                    if (!protected_mode) {pending_irq=0xa;pending_line=0;}
                    else if (!(pic_mask&1) && !(pic_service&1)) {
                        pending_irq=pic_base;pending_line=0;
                    }
                    timer_pending=0;
                }
            }
            if (pending_irq>=0) cpui386_raise_irq(cpu);
        }
        if (!(rounds%2)) mouse_event();
    }
    cpui386_delete(cpu);cpu=0;ram=ports=0;
    return result;
}
