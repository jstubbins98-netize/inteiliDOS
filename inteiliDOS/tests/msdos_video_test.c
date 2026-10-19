/* Host-side VGA semantics tests. No physical I/O or native disk access. */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "../kernel/msdos/video.h"
static uint8_t ram[0x110000],font[8192];
static uint16_t r[12];
void *kmemset(void *p,int b,size_t n) {return memset(p,b,n);}
void *kmemcpy(void *p,const void *q,size_t n) {return memcpy(p,q,n);}
void *kcalloc(size_t n,size_t size) {return calloc(n,size);}
void kfree(void *p) {free(p);}
static void reg(unsigned port,unsigned index,unsigned value) {
    assert(msdos_video_out(port,index));assert(msdos_video_out(port+1,value));
}
static void mode(unsigned n) {
    memset(r,0,sizeof(r));r[0]=n;assert(msdos_video_bios(r));
}
static uint8_t plane(unsigned p,unsigned a) {
    reg(0x3ce,4,p);return msdos_video_read(0xa0000+a);
}
static void pixel(unsigned page,unsigned x,unsigned y,unsigned color) {
    memset(r,0,sizeof(r));r[0]=0xc00|color;r[1]=x;r[2]=y;r[3]=page<<8;
    assert(msdos_video_bios(r));
}
int main(void) {
    assert(!msdos_video_reset(ram));memset(font,255,sizeof(font));msdos_video_font(font);
    static const unsigned modes[]={0xd,0xe,0xf,0x10,0x11,0x12,0x13};
    static const unsigned widths[]={320,640,640,640,640,640,320};
    static const unsigned heights[]={200,200,350,350,480,480,200};
    for (unsigned i=0;i<7;i++) {
        mode(modes[i]);assert(msdos_video_active());assert(ram[0x449]==modes[i]);
        assert(msdos_video_width()==widths[i] && msdos_video_height()==heights[i]);
        assert(msdos_video_mapped(0xa0000));assert(!msdos_video_mapped(0xb0000));
        pixel(0,3,2,modes[i]==0x13?201:1);
        r[0]=0xd00;r[1]=3;r[2]=2;r[3]=0;msdos_video_bios(r);
        assert((r[0]&255)==(modes[i]==0x13?201:1));
    }
    mode(0xd);
    reg(0x3c4,2,1);msdos_video_write(0xa0000,0xa5);
    reg(0x3c4,2,2);msdos_video_write(0xa0000,0x3c);
    assert(plane(0,0)==0xa5 && plane(1,0)==0x3c && plane(2,0)==0);
    /* Write mode 1: source latches copied, CPU byte ignored. */
    reg(0x3c4,2,15);reg(0x3ce,5,1);
    (void)msdos_video_read(0xa0000);msdos_video_write(0xa0010,0xff);
    assert(plane(0,0x10)==0xa5 && plane(1,0x10)==0x3c);
    /* Write mode 2 expands four CPU bits into individual plane bytes. */
    reg(0x3ce,5,2);msdos_video_write(0xa0020,5);
    assert(plane(0,0x20)==255 && plane(1,0x20)==0 && plane(2,0x20)==255);
    /* Read mode 1 / color compare and don't-care. */
    reg(0x3ce,2,5);reg(0x3ce,7,15);reg(0x3ce,5,8);
    assert(msdos_video_read(0xa0020)==255);
    reg(0x3ce,2,4);assert(msdos_video_read(0xa0020)==0);
    /* Write mode 3 uses rotated input as a mask over latched memory. */
    reg(0x3ce,5,3);reg(0x3ce,0,10);reg(0x3ce,8,0xf0);
    (void)msdos_video_read(0xa0020);msdos_video_write(0xa0020,0xff);
    reg(0x3ce,5,0);
    assert(plane(0,0x20)==15 && plane(1,0x20)==0xf0);
    /* Set/reset, XOR and bit mask use old latches, not destination bytes. */
    reg(0x3ce,0,1);reg(0x3ce,1,15);reg(0x3ce,3,0x18);reg(0x3ce,8,0xff);
    (void)msdos_video_read(0xa0020);msdos_video_write(0xa0020,0);
    assert(plane(0,0x20)==0xf0 && plane(1,0x20)==0xf0);
    /* Chain-4 and unchained Mode X map independent planes. */
    mode(0x13);
    assert(msdos_video_palette()[16*3]==0 && msdos_video_palette()[17*3]==5);
    assert(msdos_video_palette()[32*3+2]==63 && msdos_video_palette()[33*3]==16);
    assert(msdos_video_palette()[248*3]==0);
    for (unsigned i=0;i<4;i++) msdos_video_write(0xa0000+i,40+i);
    for (unsigned i=0;i<4;i++) assert(msdos_video_pixel(i,0)==40+i);
    reg(0x3c4,4,6);reg(0x3c4,2,4);msdos_video_write(0xa0000+80,200);
    assert(msdos_video_pixel(2,1)==200);
    reg(0x3d4,0xd,80);assert(msdos_video_pixel(2,0)==200);
    reg(0x3d4,0xd,0);
    /* DAC programming, index auto-increment, mask and readback. */
    msdos_video_out(0x3c8,200);
    msdos_video_out(0x3c9,63);msdos_video_out(0x3c9,12);msdos_video_out(0x3c9,7);
    assert(msdos_video_palette()[600]==63 && msdos_video_palette()[601]==12);
    msdos_video_out(0x3c7,200);uint8_t byte;
    msdos_video_in(0x3c9,&byte);assert(byte==63);
    msdos_video_in(0x3c9,&byte);assert(byte==12);
    msdos_video_in(0x3c9,&byte);assert(byte==7);
    msdos_video_out(0x3c6,15);assert(msdos_video_pixel(2,1)==8);
    /* BIOS pages and no-clear mode changes. */
    mode(0xd);pixel(1,0,0,4);
    r[0]=0x501;msdos_video_bios(r);assert(msdos_video_pixel(0,0)==4);
    r[0]=0xd00;r[1]=r[2]=0;r[3]=0x100;msdos_video_bios(r);assert((r[0]&255)==4);
    mode(0x8d);reg(0x3ce,4,2);assert(msdos_video_read(0xa2000)&128);
    mode(0xd);assert(msdos_video_read(0xa2000)==0);
    /* Attribute palette and flip-flop reset via status reads. */
    pixel(0,0,0,1);msdos_video_in(0x3da,&byte);
    msdos_video_out(0x3c0,0x21);msdos_video_out(0x3c0,63);
    assert(msdos_video_pixel(0,0)==63);
    /* Graphics font pointers, character drawing, and bounded string writes. */
    r[0]=0x1130;r[3]=0x300;msdos_video_bios(r);
    assert(r[8]==0xc000 && r[5]==0xa000 && r[1]==8);
    r[0]=0x941;r[3]=2;r[1]=1;msdos_video_bios(r);
    r[0]=0xd00;r[1]=r[2]=0;r[3]=0;msdos_video_bios(r);assert((r[0]&255)==2);
    r[0]=0x1a00;msdos_video_bios(r);assert((r[0]&255)==0x1a && r[3]==8);
    r[0]=3;assert(!msdos_video_bios(r));assert(!msdos_video_active());
    assert(!msdos_video_mapped(0xa0000));
    msdos_video_release();
    puts("PASS: EGA/VGA modes, planar/latch modes 0-3, read modes, chain-4, Mode X, DAC, pages, BIOS pixels/fonts, legacy handoff");
}
