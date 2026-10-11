/* Convert emulated CGA/Hercules pixels to native VGA; restore font/palette.
 * Guest video/PIT ports remain software registers, never passed through. */
#include "msdos.h"
#include "../timer.h"
#include "../vga.h"
#include "../memory.h"
static uint8_t font[8192], palette[768];
static int graphics;
static uint32_t sound;
static uint8_t inb(uint16_t p) {
    uint8_t v;__asm__ volatile("inb %1,%0":"=a"(v):"Nd"(p));return v;
}
static void outb(uint16_t p,uint8_t v) {
    __asm__ volatile("outb %0,%1"::"a"(v),"Nd"(p));
}
static void indexed(uint16_t port,uint8_t index,uint8_t value) {
    outb(port,index);outb(port+1,value);
}
static void font_access(void) {
    indexed(0x3c4,2,4);indexed(0x3c4,4,7);
    indexed(0x3ce,4,2);indexed(0x3ce,5,0);indexed(0x3ce,6,4);
}
static void enter_graphics(void) {
    static const uint8_t seq[]={3,1,15,0,14};
    static const uint8_t crtc[]={
        0x5f,0x4f,0x50,0x82,0x54,0x80,0xbf,0x1f,0,0x41,0,0,0,0,0,0,
        0x9c,0x0e,0x8f,0x28,0x40,0x96,0xb9,0xa3,0xff};
    static const uint8_t gc[]={0,0,0,0,0,0x40,5,15,255};
    static const uint8_t ac[]={0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,
                              0x41,0,15,0,0};
    outb(0x3c7,0);
    for (unsigned i=0;i<768;i++) palette[i]=inb(0x3c9);
    font_access();
    for (unsigned i=0;i<8192;i++) font[i]=((volatile uint8_t *)0xa0000)[i];
    indexed(0x3c4,0,1);outb(0x3c2,0x63);
    for (unsigned i=1;i<5;i++) indexed(0x3c4,i,seq[i]);
    indexed(0x3c4,0,3);
    outb(0x3d4,0x11);outb(0x3d5,inb(0x3d5)&127);
    for (unsigned i=0;i<25;i++) indexed(0x3d4,i,crtc[i]);
    for (unsigned i=0;i<9;i++) indexed(0x3ce,i,gc[i]);
    (void)inb(0x3da);
    for (unsigned i=0;i<21;i++) {outb(0x3c0,i);outb(0x3c0,ac[i]);}
    outb(0x3c0,0x20);
    outb(0x3c8,0);
    for (unsigned i=0;i<256;i++) {
        unsigned color=i&15, base=color&8?21:0;
        outb(0x3c9,base+((color&4)?42:0));
        outb(0x3c9,base+((color&2)?42:0));
        outb(0x3c9,base+((color&1)?42:0));
    }
    graphics=1;
}
static void leave_graphics(void) {
    if (!graphics) return;
    /* Leave chain-4 graphics before mapping font RAM. In graphics mode the
     * VGA renderer may retain graphics latches while plane 2 is rewritten. */
    vga_set_mode3();
    font_access();
    for (unsigned i=0;i<8192;i++) ((volatile uint8_t *)0xa0000)[i]=font[i];
    vga_set_mode3();
    outb(0x3c8,0);
    for (unsigned i=0;i<768;i++) outb(0x3c9,palette[i]);
    graphics=0;
}
int msdos_devices_graphics(void) {return graphics;}
void msdos_devices_restore(void) {
    leave_graphics();speaker_off();sound=0;
}
void msdos_devices_update(const uint8_t *ram,const uint8_t *ports) {
    uint32_t divisor=ram[0x4aa]|((uint32_t)ram[0x4ab]<<8);
    uint32_t tone=(ports[0x61]&3)==3 ? (divisor?divisor:65536) : 0;
    if (tone!=sound) {
        if (tone) speaker_on_divisor(tone);else speaker_off();
        sound=tone;
    }
    if (!(ports[0x3b8]&2)) {leave_graphics();return;}
    if (!graphics) enter_graphics();
    volatile uint8_t *pixels=(volatile uint8_t *)0xa0000;
    int cga=ram[0x4ac], mono=cga && ram[0x449]==6;
    unsigned color_control=ports[0x3d9]==255?0x30:ports[0x3d9];
    uint32_t base=cga ? 0xb8000 : (ports[0x3b8]&128?0xb8000:0xb0000);
    for (unsigned y=0;y<200;y++) {
        for (unsigned x=0;x<320;x++) {
            unsigned color;
            if (cga) {
                unsigned address=(y/2)*80+(y&1)*8192;
                if (mono) {
                    uint8_t bits=ram[base+address+x/4];
                    color=(bits>>(6-2*(x&3)))&3 ? 15 : 0;
                } else {
                    unsigned pair=(ram[base+address+x/4]>>(6-2*(x&3)))&3;
                    if (!pair) color=color_control&15;
                    else {
                        static const uint8_t cg[2][3]={{2,4,6},{3,5,7}};
                        color=cg[!!(color_control&32)][pair-1] |
                              (color_control&16?8:0);
                    }
                }
            } else {
                unsigned hx=x*720/320,hy=y*348/200;
                unsigned address=(hy&3)*8192+(hy/4)*90+hx/8;
                color=ram[base+address]&(128>>(hx&7)) ? 15 : 0;
            }
            pixels[y*320+x]=color;
        }
    }
}
