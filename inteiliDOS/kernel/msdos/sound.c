/* Isolated Sound Blaster 2 / SB16 PCM + OPL playback. MPL-2.0.
 * No guest DMA, PIC or DSP accesses are forwarded to native hardware. */
#include "sound.h"
#include "fm.h"
struct dma {
    uint16_t address,base_address,count,base_count;
    uint8_t page,mode,masked,flip,tc;
};
static unsigned selected;
static struct {
    uint8_t *ram;
    unsigned size,model,rate,phase,block,left;
    struct dma dma[2]; /* ISA channels 1 (bytes) and 5 (words) */
    uint8_t reset,speaker,paused,active,automatic,wide,stereo,sign;
    int16_t output[2];
    uint8_t command,args[3],need,have,reply[64],rp,wp,test;
    uint8_t irq,edge,service,pic_mask,pic_base,pic_init,pic_read,error,pic_icw1;
    uint8_t identify_value,identify_xor,identify_pending;
    uint8_t mixer_index,mixer[256];
} s;
void msdos_sound_select(unsigned model) {selected=model;}
unsigned msdos_sound_model(void) {return selected;}
static void mixer_reset(void) {
    s.mixer[0x30]=s.mixer[0x31]=s.mixer[0x32]=s.mixer[0x33]=0xf8;
    s.mixer[0x34]=s.mixer[0x35]=0xf8;
}
static void reply(uint8_t value) {
    if ((uint8_t)(s.wp-s.rp)<64) s.reply[s.wp++&63]=value;
}
static void interrupt(unsigned wide) {
    unsigned bit=wide?2:1;
    if (!(s.irq&bit)) s.edge=1;
    s.irq|=bit;
}
static void acknowledge(unsigned bit) {
    s.irq&=~bit;
    if (!s.irq) s.edge=0;
}
void msdos_sound_reset(unsigned model,uint8_t *ram,unsigned size) {
    s=(typeof(s)){0};
    selected=s.model=model;s.ram=ram;s.size=size;
    s.rate=8000;s.block=65536;s.dma[0].masked=s.dma[1].masked=1;
    s.pic_mask=255;s.pic_base=8;
    s.identify_value=0xaa;s.identify_xor=0x96;
    mixer_reset();
    msdos_fm_reset(model);
}
void msdos_sound_stop(void) {
    s=(typeof(s)){0};
    msdos_fm_reset(MSDOS_SOUND_OFF);
}
static void dma_advance(struct dma *d) {
    d->address+=(d->mode&0x20)?-1:1;
    if (!d->count--) {
        d->tc=1;
        if (d->mode&0x10) {d->address=d->base_address;d->count=d->base_count;}
        else d->masked=1;
    }
}
static void identify_dma(void) {
    struct dma *d=&s.dma[0];
    if (!s.identify_pending || d->masked || (d->mode&12)!=4) return;
    unsigned address=((unsigned)d->page<<16)|d->address;
    if (address>=s.size) {s.identify_pending=0;s.error=0xff;return;}
    s.ram[address]=s.identify_value;
    dma_advance(d);s.identify_pending=0;
}
static void start(unsigned automatic,unsigned block,unsigned wide,unsigned mode) {
    s.active=1;s.paused=0;s.automatic=automatic;
    s.block=block;s.left=block;s.phase=0;s.wide=wide;
    s.stereo=!!(mode&0x20);s.sign=!!(mode&0x10);
}
static void command(uint8_t c) {
    s.command=c;s.have=0;s.need=0;
    if ((c&0xe0)==0xa0 || (c&0xf0)==0xc0) {
        /* Bx/Cx: output/input, auto-init, FIFO. Recording is not supported. */
        if ((c&0xf0)!=0xb0 && (c&0xf0)!=0xc0) {s.error=c;return;}
        if (s.model!=MSDOS_SOUND_SB16) {s.error=c;return;}
        s.need=3;return;
    }
    switch (c) {
    case 0x10:case 0x40:case 0xe0:case 0xe2:case 0xe4:s.need=1;break;
    case 0x41:case 0x42:
        if (s.model!=MSDOS_SOUND_SB16) {s.error=c;break;}
        s.need=2;break;
    case 0x14:case 0x48:case 0x80:s.need=2;break;
    case 0x1c:case 0x90:start(1,s.block,0,0);break;
    case 0x91:start(0,s.block,0,0);break;
    case 0xd0:if (!s.wide) s.paused=1;break;
    case 0xd4:if (!s.wide) s.paused=0;break;
    case 0xd5:case 0xd6:case 0xd9:
        if (s.model!=MSDOS_SOUND_SB16) {s.error=c;break;}
        if (s.wide) {
            if (c==0xd9) s.automatic=0;else s.paused=c==0xd5;
        }
        break;
    case 0xd1:s.speaker=1;break;
    case 0xd3:s.speaker=0;break;
    case 0xd8:reply(s.speaker?255:0);break;
    case 0xda:if (!s.wide) s.automatic=0;break;
    case 0xe1:reply(s.model==MSDOS_SOUND_SB16?4:2);reply(s.model==MSDOS_SOUND_SB16?5:1);break;
    case 0xe3:
        if (s.model==MSDOS_SOUND_SB16) {
            const char text[]="COPYRIGHT (C) CREATIVE TECHNOLOGY LTD, 1992.";
            for (unsigned i=0;i<sizeof(text);i++) reply(text[i]);
        } else s.error=c;
        break;
    case 0xe8:reply(s.test);break;
    case 0xf2:interrupt(0);break;
    case 0xf3:if (s.model==MSDOS_SOUND_SB16) interrupt(1);else s.error=c;break;
    default:s.error=c;break;
    }
}
static void argument(uint8_t value) {
    s.args[s.have++]=value;
    if (s.have<s.need) return;
    s.need=0;
    unsigned block=1u+s.args[0]+((unsigned)s.args[1]<<8);
    if ((s.command&0xf0)==0xb0 || (s.command&0xf0)==0xc0) {
        if ((s.command&9) || (s.args[0]&~0x30)) {s.error=s.command;return;}
        start(!!(s.command&4),1u+s.args[1]+((unsigned)s.args[2]<<8),
              (s.command&0xf0)==0xb0,s.args[0]);
        s.speaker=1;return;
    }
    switch (s.command) {
    case 0x10:s.output[0]=s.output[1]=((int)value-128)*256;break;
    case 0x40:s.rate=1000000u/(256u-value);if (s.rate>48000) s.rate=48000;break;
    case 0x41:case 0x42:
        if (s.command==0x42) break; /* input rate, no recording device */
        s.rate=((unsigned)s.args[0]<<8)|s.args[1];
        if (!s.rate || s.rate>48000) {s.error=s.command;s.rate=8000;}
        break;
    case 0x14:start(0,block,0,0);break;
    case 0x48:s.block=block;break;
    case 0x80:start(0,block,0,0);s.active=2;break;
    case 0xe0:reply(~value);break;
    case 0xe2:
        s.identify_value+=(uint8_t)(value^s.identify_xor);
        s.identify_xor=(uint8_t)((s.identify_xor>>2)|(s.identify_xor<<6));
        s.identify_pending=1;identify_dma();break;
    case 0xe4:s.test=value;break;
    }
}
static int dma_port(unsigned p,uint8_t *v,int write) {
    for (unsigned i=0;i<2;i++) {
        if (i && s.model!=MSDOS_SOUND_SB16) continue;
        unsigned port=i?0xc4:2,count=i?0xc6:3,page=i?0x8b:0x83;
        unsigned base=i?0xd0:8,step=i?2:1;
        struct dma *d=&s.dma[i];
        if (p==port || p==count) {
            uint16_t *current=p==port?&d->address:&d->count;
            if (write) {
                *current=d->flip?(*current&255)|((uint16_t)*v<<8):(*current&0xff00)|*v;
                if (p==port) d->base_address=*current;else d->base_count=*current;
            } else *v=*current>>(d->flip?8:0);
            d->flip^=1;return 1;
        }
        if (p==page) {if (write) d->page=*v;else *v=d->page;return 1;}
        if (!write) {
            if (p==base) {*v=d->tc?2:0;d->tc=0;return 1;}
            continue;
        }
        if (p==base+2*step) {if ((*v&3)==1) d->masked=!!(*v&4);}
        else if (p==base+3*step) {if ((*v&3)==1) d->mode=*v;}
        else if (p==base+4*step) d->flip=0;
        else if (p==base+5*step) {d->flip=0;d->masked=1;d->tc=0;}
        else if (p==base+6*step) d->masked=0;
        else if (p==base+7*step) d->masked=!!(*v&2);
        else continue;
        return 1;
    }
    return 0;
}
static uint8_t mixer_read(void) {
    switch (s.mixer_index) {
    case 0x80:return 2; /* fixed IRQ5 */
    case 0x81:return 0x22; /* DMA1 and DMA5 */
    case 0x82:return s.irq;
    case 0x22:return (s.mixer[0x30]&0xf0)|(s.mixer[0x31]>>4);
    case 4:return (s.mixer[0x32]&0xf0)|(s.mixer[0x33]>>4);
    case 0x26:return (s.mixer[0x34]&0xf0)|(s.mixer[0x35]>>4);
    default:return s.mixer[s.mixer_index];
    }
}
int msdos_sound_out(unsigned p,uint8_t v) {
    if (msdos_fm_out(p,v)) return 1;
    if (p>=0x220 && p<=0x22f) {
        if (!s.model) return 1;
        if (p==0x224 && s.model==MSDOS_SOUND_SB16) s.mixer_index=v;
        else if (p==0x225 && s.model==MSDOS_SOUND_SB16) {
            unsigned r=s.mixer_index;
            if (!r) mixer_reset();
            else if (r==0x22 || r==4 || r==0x26) {
                unsigned first=r==4?0x32:r==0x26?0x34:0x30;
                s.mixer[first]=((v>>4)*31/15)<<3;
                s.mixer[first+1]=((v&15)*31/15)<<3;
            } else if (r<0x80) s.mixer[r]=v;
        } else if (p==0x226) {
            if (v&1) {
                s.reset=1;s.active=s.speaker=s.paused=s.need=0;
                s.irq=s.edge=s.service=0;s.rp=s.wp=0;s.error=0;
                s.output[0]=s.output[1]=0;
                s.identify_value=0xaa;s.identify_xor=0x96;s.identify_pending=0;
            } else if (s.reset) {s.reset=0;reply(0xaa);}
        } else if (p==0x22c && !s.reset) {
            if (s.need) argument(v);else command(v);
        }
        return 1;
    }
    if (!s.model) return 0;
    if (dma_port(p,&v,1)) {identify_dma();return 1;}
    switch (p) {
    case 0x20:
        /* Ignore the original XT ROM's unused-port FF sweep as an ICW1. */
        if ((v&0xf0)==0x10) {s.pic_init=1;s.pic_mask=255;s.pic_icw1=v;}
        else if ((v&0xe0)==0x20 || ((v&0xe0)==0x60 && (v&7)==5)) s.service=0;
        else if ((v&0x18)==8) s.pic_read=v&3;
        break;
    case 0x21:
        if (s.pic_init) {
            if (s.pic_init==1) {
                s.pic_base=v&0xf8;
                s.pic_init=(s.pic_icw1&2)?((s.pic_icw1&1)?3:0):2;
            } else if (s.pic_init==2) s.pic_init=(s.pic_icw1&1)?3:0;
            else s.pic_init=0;
        } else s.pic_mask=v;
        break;
    default:return 0;
    }
    return 1;
}
int msdos_sound_in(unsigned p,uint8_t *v) {
    if (msdos_fm_in(p,v)) return 1;
    if (p>=0x220 && p<=0x22f) {
        *v=255;
        if (!s.model) return 1;
        if (p==0x225 && s.model==MSDOS_SOUND_SB16) *v=mixer_read();
        else if (p==0x224 && s.model==MSDOS_SOUND_SB16) *v=s.mixer_index;
        else if (p==0x22a) *v=s.rp!=s.wp?s.reply[s.rp++&63]:255;
        else if (p==0x22c) *v=0;
        else if (p==0x22e) {*v=s.rp!=s.wp?0x80:0;acknowledge(1);}
        else if (p==0x22f && s.model==MSDOS_SOUND_SB16) {*v=255;acknowledge(2);}
        return 1;
    }
    if (!s.model) return 0;
    if (dma_port(p,v,0)) return 1;
    switch (p) {
    case 0x21:*v=s.pic_mask;break;
    case 0x20:*v=(s.pic_read==3?s.service:s.edge)?0x20:0;break;
    default:return 0;
    }
    return 1;
}
static int next_value(int16_t *value) {
    struct dma *d=&s.dma[s.wide];
    if (d->masked || (d->mode&12)!=8) return 0;
    unsigned address=s.wide?((unsigned)(d->page&0xfe)<<16)|((unsigned)d->address<<1):
        ((unsigned)d->page<<16)|d->address;
    if (address>=s.size || (s.wide && address==s.size-1)) {
        s.active=0;s.output[0]=s.output[1]=0;s.error=0xff;return 0;
    }
    unsigned raw=s.ram[address];
    if (s.wide) raw|=(unsigned)s.ram[address+1]<<8;
    *value=s.wide?(s.sign?(int16_t)raw:(int)raw-32768):
        (s.sign?(int)(int8_t)raw*256:((int)raw-128)*256);
    dma_advance(d);
    if (!--s.left) {
        interrupt(s.wide);
        if (s.automatic) s.left=s.block;else s.active=0;
    }
    return 1;
}
static void sample(void) {
    if (!s.active || s.paused) return;
    if (s.active==2) {
        s.output[0]=s.output[1]=0;
        if (!--s.left) {interrupt(0);s.active=0;}
        return;
    }
    int16_t left,right;
    if (!next_value(&left)) return;
    right=left;
    if (s.stereo && s.active) next_value(&right);
    s.output[0]=left;s.output[1]=right;
}
void msdos_sound_render(int16_t *output,unsigned frames) {
    for (unsigned i=0;i<frames;i++) {
        int16_t fm[2];
        msdos_fm_sample(fm);
        if (s.model) {
            s.phase+=s.rate;
            if (s.phase>=48000) {s.phase-=48000;sample();}
        }
        for (unsigned channel=0;channel<2;channel++) {
            int value=s.model && s.speaker?s.output[channel]:0;
            int music=fm[channel];
            if (s.model==MSDOS_SOUND_SB16) {
                value=value*(s.mixer[0x32+channel]>>3)/31;
                music=music*(s.mixer[0x34+channel]>>3)/31;
            }
            value+=music;
            if (s.model==MSDOS_SOUND_SB16)
                value=value*(s.mixer[0x30+channel]>>3)/31;
            if (value>32767) value=32767;
            if (value<-32768) value=-32768;
            output[i*2+channel]=value;
        }
    }
}
int msdos_sound_irq_pending(void) {
    return s.model && s.edge && !s.service && !(s.pic_mask&0x20);
}
int msdos_sound_irq(void) {
    if (!msdos_sound_irq_pending()) return -1;
    s.edge=0;s.service=1;return s.pic_base+5;
}
int msdos_sound_error(void) {return s.error;}
