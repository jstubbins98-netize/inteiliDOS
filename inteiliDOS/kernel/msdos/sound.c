/* Sound Blaster-compatible 8-bit playback subset. MPL-2.0.
 * Guest DMA, PIC and DSP state are private, never native ISA DMA/PIC ports. */
#include "sound.h"
static struct {
    const uint8_t *ram;
    unsigned size,enabled,rate,phase,block,left;
    uint16_t address,base_address,count,base_count;
    uint8_t page,mode,masked,flip,tc,reset,speaker,paused,active,automatic;
    uint8_t sample,command,args[2],need,have,reply[16],rp,wp,test;
    uint8_t irq,edge,service,pic_mask,pic_base,pic_init,pic_read,error,pic_icw1;
} s;
static void reply(uint8_t value) {
    if ((uint8_t)(s.wp-s.rp)<16) s.reply[s.wp++&15]=value;
}
static void interrupt(void) {
    if (!s.irq) s.edge=1;
    s.irq=1;
}
void msdos_sound_reset(int enabled,const uint8_t *ram,unsigned size) {
    s=(typeof(s)){0};
    s.enabled=!!enabled;s.ram=ram;s.size=size;
    s.rate=8000;s.block=65536;s.masked=1;s.sample=128;
    s.pic_mask=255;s.pic_base=8;
}
static void start(unsigned automatic,unsigned block) {
    s.active=1;s.paused=0;s.automatic=automatic;
    s.left=block;s.phase=0;
}
static void command(uint8_t c) {
    s.command=c;s.have=0;s.need=0;
    switch (c) {
    case 0x10:case 0x40:case 0xe0:case 0xe4:s.need=1;break;
    case 0x14:case 0x48:case 0x80:s.need=2;break;
    case 0x1c:case 0x90:start(1,s.block);break;
    case 0x91:start(0,s.block);break;
    case 0xd0:s.paused=1;break;
    case 0xd4:s.paused=0;break;
    case 0xd1:s.speaker=1;break;
    case 0xd3:s.speaker=0;break;
    case 0xd8:reply(s.speaker?255:0);break;
    case 0xda:s.automatic=0;break; /* finish the current block */
    case 0xe1:reply(2);reply(1);break;
    case 0xe8:reply(s.test);break;
    case 0xf2:interrupt();break;
    default:s.error=c;break;
    }
}
static void argument(uint8_t value) {
    s.args[s.have++]=value;
    if (s.have<s.need) return;
    s.need=0;
    unsigned block=1u+s.args[0]+((unsigned)s.args[1]<<8);
    switch (s.command) {
    case 0x10:s.sample=value;break;
    case 0x40:
        s.rate=1000000u/(256u-value);
        if (s.rate>48000) s.rate=48000;
        break;
    case 0x14:start(0,block);break;
    case 0x48:s.block=block;break;
    case 0x80:start(0,block);s.active=2;break;
    case 0xe0:reply(~value);break;
    case 0xe4:s.test=value;break;
    }
}
int msdos_sound_out(unsigned p,uint8_t v) {
    if (p>=0x220 && p<=0x22f) {
        if (!s.enabled) return 1;
        if (p==0x226) {
            if (v&1) {
                s.reset=1;s.active=s.speaker=s.paused=s.need=0;
                s.irq=s.edge=s.service=0;s.rp=s.wp=0;s.sample=128;s.error=0;
            } else if (s.reset) {s.reset=0;reply(0xaa);}
        } else if (p==0x22c && !s.reset) {
            if (s.need) argument(v);else command(v);
        }
        return 1;
    }
    if (!s.enabled) return 0;
    switch (p) {
    case 2:case 3: {
        uint16_t *current=p==2?&s.address:&s.count;
        *current=s.flip?(*current&255)|((uint16_t)v<<8):(*current&0xff00)|v;
        s.flip^=1;
        if (p==2) s.base_address=*current;else s.base_count=*current;
        break;
    }
    case 0x83:s.page=v;break;
    case 0x0a:if ((v&3)==1) s.masked=!!(v&4);break;
    case 0x0b:if ((v&3)==1) s.mode=v;break;
    case 0x0c:s.flip=0;break;
    case 0x0d:s.flip=0;s.masked=1;s.tc=0;break;
    case 0x0e:s.masked=0;break;
    case 0x0f:s.masked=!!(v&2);break;
    case 0x20:
        /* ICW1's upper three bits are reserved. The bundled XT firmware
         * fills unused ports with FF; that is not a valid PIC initialization. */
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
    if (p>=0x220 && p<=0x22f) {
        *v=255;
        if (!s.enabled) return 1;
        if (p==0x22a) *v=s.rp!=s.wp?s.reply[s.rp++&15]:255;
        else if (p==0x22c) *v=0; /* write buffer ready */
        else if (p==0x22e) {
            *v=s.rp!=s.wp?0x80:0;s.irq=0;s.edge=0;
        }
        return 1;
    }
    if (!s.enabled) return 0;
    switch (p) {
    case 2:case 3:
        *v=(p==2?s.address:s.count)>>(s.flip?8:0);s.flip^=1;break;
    case 8:*v=s.tc?2:0;s.tc=0;break;
    case 0x83:*v=s.page;break;
    case 0x21:*v=s.pic_mask;break;
    case 0x20:*v=(s.pic_read==3?s.service:s.edge)?0x20:0;break;
    default:return 0;
    }
    return 1;
}
static void sample(void) {
    if (!s.active || s.paused) return;
    if (s.active==1) {
        if (s.masked || (s.mode&12)!=8) return;
        unsigned address=((unsigned)s.page<<16)|s.address;
        if (address>=s.size) {s.active=0;s.sample=128;s.error=0xff;return;}
        s.sample=s.ram[address];
        s.address+=(s.mode&0x20)?-1:1;
        if (!s.count--) {
            s.tc=1;
            if (s.mode&0x10) {s.address=s.base_address;s.count=s.base_count;}
            else s.masked=1;
        }
    } else s.sample=128;
    if (!--s.left) {
        interrupt();
        if (s.automatic) s.left=s.block;else s.active=0;
    }
}
void msdos_sound_render(int16_t *output,unsigned frames) {
    for (unsigned i=0;i<frames;i++) {
        if (s.enabled) {
            s.phase+=s.rate;
            if (s.phase>=48000) {s.phase-=48000;sample();}
        }
        int16_t value=s.enabled && s.speaker?((int)s.sample-128)*256:0;
        output[i*2]=output[i*2+1]=value;
    }
}
int msdos_sound_irq_pending(void) {
    return s.enabled && s.edge && !s.service && !(s.pic_mask&0x20);
}
int msdos_sound_irq(void) {
    if (!msdos_sound_irq_pending()) return -1;
    s.edge=0;s.service=1;return s.pic_base+5;
}
int msdos_sound_error(void) {return s.error;}
