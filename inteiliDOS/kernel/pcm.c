/* Bounded polled PCI PCM output. No ISA DMA or native PIC/PIT changes.
 * AC'97: Intel ICH bus-master interface only, not arbitrary class-04 devices.
 * HDA: discover an analog pin -> DAC path, not hardcoded codec node IDs.
 * All controller DMA references statically allocated native buffers. MPL-2.0. */
#include "pcm.h"
#include "config.h"
#if CONFIG_ENABLE_PCI
#include "pci.h"
#include "timer.h"
#include "ac97.h"
#include "hda.h"
#define FRAMES 480u
#define BYTES (FRAMES*4)
#define QUEUE 4u
/* The AC'97 controller cycles 32 descriptors, but only QUEUE are valid at
 * once. Reuse four physical buffers after their descriptors are consumed. */
static int16_t samples[QUEUE][FRAMES*2] __attribute__((aligned(128)));
static struct {uint32_t address,length;} ac_bdl[32] __attribute__((aligned(128)));
static struct {uint32_t low,high,length,flags;} hd_bdl[QUEUE] __attribute__((aligned(128)));
static uint32_t corb[256] __attribute__((aligned(128)));
static volatile struct {uint32_t data,extra;} rirb[256] __attribute__((aligned(128)));
static pci_device_t device;
static uint16_t saved_command,nam,bm;
static uint32_t mmio,stream;
static unsigned backend,cursor,tail;
static const char *failure="no supported output controller";
static uint8_t wp,rp,codec,pin,dac,audio_group;
static void (*produce)(int16_t *,unsigned);
static uint8_t in8(uint16_t p) {uint8_t v;__asm__ volatile("inb %1,%0":"=a"(v):"Nd"(p));return v;}
static uint16_t in16(uint16_t p) {uint16_t v;__asm__ volatile("inw %1,%0":"=a"(v):"Nd"(p));return v;}
static void out8(uint16_t p,uint8_t v) {__asm__ volatile("outb %0,%1"::"a"(v),"Nd"(p));}
static void out16(uint16_t p,uint16_t v) {__asm__ volatile("outw %0,%1"::"a"(v),"Nd"(p));}
static void out32(uint16_t p,uint32_t v) {__asm__ volatile("outl %0,%1"::"a"(v),"Nd"(p));}
static uint32_t rd(unsigned o) {return *(volatile uint32_t *)(mmio+o);}
static void wr(unsigned o,uint32_t v) {*(volatile uint32_t *)(mmio+o)=v;}
static uint8_t r8(unsigned o) {return *(volatile uint8_t *)(mmio+o);}
static uint16_t r16(unsigned o) {return *(volatile uint16_t *)(mmio+o);}
static void w8(unsigned o,uint8_t v) {*(volatile uint8_t *)(mmio+o)=v;}
static void w16(unsigned o,uint16_t v) {*(volatile uint16_t *)(mmio+o)=v;}
static int waitbit(unsigned o,unsigned mask,unsigned value) {
    for (unsigned n=0;n<100000;n++) if ((rd(o)&mask)==value) return 1;
    return 0;
}
static void fill(unsigned index) {
    produce(samples[index%QUEUE],FRAMES);
    __asm__ volatile("":::"memory");
}
static void claim(const pci_device_t *d) {
    device=*d;saved_command=pci_read16(d->bus,d->device,d->function,4);
    pci_write16(d->bus,d->device,d->function,4,saved_command|7);
}
static void release(void) {
    pci_write16(device.bus,device.device,device.function,4,saved_command);
}
static int intel_ac97(uint16_t id) {
    switch (id) {
    case 0x2415:case 0x2425:case 0x2445:case 0x2485:case 0x24c5:
    case 0x24d5:case 0x25a6:case 0x266e:case 0x27de:return 1;
    default:return 0;
    }
}
static int start_ac97(void) {
    pci_device_t d,previous;int found=pci_find_class(4,1,&d);
    while (!found) {
        if (d.vendor_id==0x8086 && intel_ac97(d.device_id) &&
            (d.bar[0]&1) && (d.bar[1]&1) &&
            (d.bar[0]&~3u)<=0xff00 && (d.bar[1]&~3u)<=0xff00) {
            ac97_stop_capture();claim(&d);
            nam=d.bar[0]&~3u;bm=d.bar[1]&~3u;
            out32(bm+0x2c,2); /* cold reset release, no controller IRQs */
            unsigned n;
            for (n=0;n<100000;n++) {
                uint32_t status;
                __asm__ volatile("inl %1,%0":"=a"(status):"Nd"((uint16_t)(bm+0x30)));
                if (status&0x100) break;
            }
            if (n==100000) {failure="AC'97 codec not ready";release();return 0;}
            out16(nam+0,0);out16(nam+2,0x0808);out16(nam+0x18,0x0808);
            /* Fixed 48 kHz DAC: do not depend on variable-rate extensions. */
            out16(nam+0x2a,in16(nam+0x2a)&~1u);
            out8(bm+0x1b,2);
            for (n=0;n<100000 && (in8(bm+0x1b)&2);n++) {}
            if (n==100000) {release();return 0;}
            for (unsigned i=0;i<32;i++) {
                ac_bdl[i].address=(uint32_t)samples[i%QUEUE];
                ac_bdl[i].length=FRAMES*2; /* 16-bit samples, no IRQ flag */
            }
            for (unsigned i=0;i<QUEUE;i++) fill(i);
            out32(bm+0x10,(uint32_t)ac_bdl);out8(bm+0x15,QUEUE-1);
            out16(bm+0x16,0x1c);cursor=0;tail=QUEUE-1;
            backend=1;out8(bm+0x1b,1);return 1;
        }
        previous=d;found=pci_find_class_after(4,1,&previous,&d);
    }
    return 0;
}
static int verb(unsigned nid,unsigned code,unsigned value,uint32_t *response) {
    unsigned next=(uint8_t)(wp+1);
    corb[next]=((uint32_t)codec<<28)|(nid<<20)|(code<<8)|value;
    __asm__ volatile("":::"memory");w16(0x48,next);wp=next;
    for (unsigned n=0;n<100000;n++) {
        uint8_t now=r16(0x58);
        if (now!=rp) {
            rp=(uint8_t)(rp+1);
            __asm__ volatile("":::"memory");
            uint32_t data=rirb[rp].data,extra=rirb[rp].extra;
            w8(0x5d,5);
            /* Ignore unsolicited events and replies from another codec. */
            if (!(extra&0x10) && (extra&15)==codec) {
                *response=data;return 1;
            }
        }
    }
    return 0;
}
static uint32_t get(unsigned nid,unsigned code,unsigned value) {
    uint32_t response=0xffffffff;
    verb(nid,code,value,&response);return response;
}
static void set(unsigned nid,unsigned code,unsigned value) {
    uint32_t ignored;verb(nid,code,value,&ignored);
}
static void payload16(unsigned nid,unsigned code,unsigned value) {
    set(nid,code|(value>>8),value&255);
}
static void unmute(unsigned nid,unsigned caps,unsigned input_index) {
    if (caps&4) {
        uint32_t amp=get(caps&8?nid:audio_group,0xf00,0x12);
        payload16(nid,0x300,0xb000|(amp&127));
    }
    if (caps&2) {
        uint32_t amp=get(caps&8?nid:audio_group,0xf00,0x0d);
        payload16(nid,0x300,0x7000|((input_index&15)<<8)|(amp&127));
    }
}
/* Bounded topology walk. Configure only a successful pin-to-DAC path. */
static int route(unsigned nid,unsigned depth,uint8_t *visited) {
    if (nid>=256 || depth>12 || visited[nid]) return 0;
    visited[nid]=1;
    uint32_t caps=get(nid,0xf00,9);
    if (caps==0xffffffff) return 0;
    unsigned type=(caps>>20)&15;
    if (!type) {
        dac=nid;set(nid,0x705,0);unmute(nid,caps,0);return 1;
    }
    if (type!=2 && type!=3 && type!=4) return 0;
    unsigned connections=get(nid,0xf00,0x0e);
    if (connections==0xffffffff) return 0;
    unsigned count=connections&127,long_form=connections&128;
    /* Range-coded lists are deliberately not guessed. */
    for (unsigned i=0;i<count && i<32;i++) {
        unsigned packed=get(nid,0xf02,i/(long_form?2:4)*(long_form?2:4));
        unsigned entry=(packed>>((i%(long_form?2:4))*(long_form?16:8)))&
                       (long_form?65535:255);
        if (entry&(long_form?0x8000:0x80)) continue;
        if (route(entry,depth+1,visited)) {
            set(nid,0x705,0);set(nid,0x701,i);unmute(nid,caps,i);
            return 1;
        }
    }
    return 0;
}
static int find_output(void) {
    uint32_t groups=get(0,0xf00,4);
    if (groups==0xffffffff) return 0;
    unsigned first=(groups>>16)&255,count=groups&255;
    for (unsigned g=first;g<first+count && g<256;g++) {
        if ((get(g,0xf00,5)&255)!=1) continue;
        audio_group=g;
        set(g,0x705,0);
        unsigned nodes=get(g,0xf00,4),start=(nodes>>16)&255,end=start+(nodes&255);
        /* Prefer speaker, then line out, then headphone pins. */
        for (unsigned wanted=0;wanted<3;wanted++)
            for (unsigned n=start;n<end && n<256;n++) {
                uint32_t caps=get(n,0xf00,9),pc=get(n,0xf00,0x0c);
                uint32_t def=get(n,0xf1c,0);
                unsigned kind=(def>>20)&15,match=wanted==0?1:wanted==1?0:2;
                if (((caps>>20)&15)!=4 || !(pc&16) || kind!=match ||
                    (def>>30)==1) continue;
                uint8_t visited[256]={0};
                if (route(n,0,visited)) {
                    pin=n;set(n,0x707,kind==2?0xc0:0x40);
                    if (pc&(1u<<16)) set(n,0x70c,2);
                    return 1;
                }
            }
    }
    return 0;
}
static void stop_hda(void) {
    if (stream) {w8(stream,0);w8(stream+3,0x1c);}
    w8(0x4c,0);w8(0x5c,0);
}
static int start_hda(void) {
    pci_device_t d,previous;int found=pci_find_class(4,3,&d);
    while (!found) {
        if (!(d.bar[0]&1) && (d.bar[0]&~15u) && (!(d.bar[0]&4) || !d.bar[1])) {
            hda_stop_capture();claim(&d);mmio=d.bar[0]&~15u;stream=0;
            wr(8,0);
            failure="HDA controller reset failed";
            if (!waitbit(8,1,0)) goto next;
            timer_sleep(1);wr(8,1);
            if (!waitbit(8,1,1)) goto next;
            timer_sleep(2);
            unsigned gcap=r16(0),iss=(gcap>>8)&15,oss=(gcap>>12)&15;
            if (!oss) goto next;
            failure="HDA command rings unsupported";
            stream=0x80+iss*0x20;
            wr(0x20,0); /* Native IRQs are never enabled. */
            w8(0x4c,0);w8(0x5c,0);
            if (!(r8(0x4e)&0x40) || !(r8(0x5e)&0x40)) goto next;
            wr(0x40,(uint32_t)corb);wr(0x44,0);
            wr(0x50,(uint32_t)rirb);wr(0x54,0);
            w8(0x4e,2);w8(0x5e,2);w16(0x4a,0x8000);
            w16(0x4a,0);w16(0x48,0);w16(0x58,0x8000);
            w16(0x5a,1);w8(0x5d,5);wp=rp=0;
            /* RINTCTL enables the response-status latch, not native IRQ
             * delivery (INTCTL remains zero). With RINTCNT=1, QEMU stops
             * accepting verbs until RINTFL is set and acknowledged. */
            w8(0x5c,3);w8(0x4c,2);
            unsigned codecs=r16(0x0e);
            /* Firmware may already have acknowledged STATESTS. It is a
             * change latch, not an authoritative map of connected codecs. */
            if (!(codecs&0x7fff)) codecs=0x7fff;
            int output=0;
            failure="no routable analog HDA output";
            for (codec=0;codec<15;codec++)
                if ((codecs&(1u<<codec)) && find_output()) {output=1;break;}
            if (!output) goto next;
            failure="HDA output stream reset failed";
            for (unsigned i=0;i<QUEUE;i++) {
                fill(i);hd_bdl[i].low=(uint32_t)samples[i];hd_bdl[i].high=0;
                hd_bdl[i].length=BYTES;hd_bdl[i].flags=0;
            }
            w8(stream,0);w8(stream,1);
            if (!waitbit(stream,1,1)) goto next;
            w8(stream,0);if (!waitbit(stream,1,0)) goto next;
            wr(stream+0x18,(uint32_t)hd_bdl);wr(stream+0x1c,0);
            wr(stream+8,QUEUE*BYTES);w16(stream+0x0c,QUEUE-1);
            w16(stream+0x12,0x11);w8(stream+2,0x10);w8(stream+3,0x1c);
            payload16(dac,0x200,0x11);set(dac,0x706,0x10);
            cursor=0;backend=2;w8(stream,2);return 1;
next:
            stop_hda();release();
        }
        previous=d;found=pci_find_class_after(4,3,&previous,&d);
    }
    return 0;
}
int pcm_start(void (*render)(int16_t *,unsigned)) {
    pcm_stop();produce=render;failure="no supported output controller";
    return start_ac97() || start_hda();
}
void pcm_poll(void) {
    if (backend==1) {
        unsigned current=in8(bm+0x14)&31;
        for (unsigned n=0;n<32 && cursor!=current;n++) {
            tail=(tail+1)&31;fill(tail);out8(bm+0x15,tail);
            cursor=(cursor+1)&31;
        }
        out16(bm+0x16,0x1c);
    } else if (backend==2) {
        unsigned current=(rd(stream+4)/BYTES)%QUEUE;
        for (unsigned n=0;n<QUEUE && cursor!=current;n++) {
            fill(cursor);cursor=(cursor+1)%QUEUE;
        }
        w8(stream+3,0x1c);
    }
}
void pcm_stop(void) {
    if (backend==1) {
        out8(bm+0x1b,0);
        for (unsigned n=0;n<100000 && !(in16(bm+0x16)&1);n++) {}
        out8(bm+0x1b,2);out16(nam+0x18,0x8000);
    } else if (backend==2) {
        w8(stream,0);
        /* Disconnect the codec while command/response DMA still runs. */
        set(pin,0x707,0);set(dac,0x706,0);stop_hda();
    }
    if (backend) release();
    backend=0;produce=0;
}
const char *pcm_backend(void) {
    return backend==1?"Intel AC'97":backend==2?"Intel HDA analog":0;
}
const char *pcm_error(void) {return failure;}
#else
int pcm_start(void (*render)(int16_t *,unsigned)) {(void)render;return 0;}
void pcm_poll(void) {}
void pcm_stop(void) {}
const char *pcm_backend(void) {return 0;}
const char *pcm_error(void) {return "native PCM requires a build with PCI enabled";}
#endif
