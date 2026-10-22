/* SB16 DSP, mixer, 16-bit DMA and stereo conversion. No hardware access. */
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include "../kernel/msdos/sound.h"
static uint8_t ram[0x40000];
static int16_t output[32];
static void out(unsigned p,unsigned v) {assert(msdos_sound_out(p,v));}
static unsigned in(unsigned p) {uint8_t v;assert(msdos_sound_in(p,&v));return v;}
static void dsp(unsigned c) {out(0x22c,c);}
static void reset(void) {
    msdos_sound_reset(MSDOS_SOUND_SB16,ram,sizeof(ram));
    out(0x226,1);out(0x226,0);assert(in(0x22a)==0xaa);
    dsp(0xe1);assert(in(0x22a)==4 && in(0x22a)==5);
    dsp(0x41);dsp(0xbb);dsp(0x80); /* 48000 samples/sec */
    out(0x21,0xdf);
}
static void dma(unsigned address,unsigned words,int automatic) {
    out(0xd4,5);out(0xd8,0);
    unsigned word=address>>1;
    out(0xc4,word);out(0xc4,word>>8);out(0x8b,address>>16);
    out(0xc6,words-1);out(0xc6,(words-1)>>8);
    out(0xd6,automatic?0x59:0x49);out(0xd4,1);
}
static void play(unsigned command,unsigned mode,unsigned samples) {
    dsp(command);dsp(mode);dsp(samples-1);dsp((samples-1)>>8);
}
static void sample(unsigned address,int n) {ram[address]=n;ram[address+1]=n>>8;}
static unsigned mix(unsigned reg) {out(0x224,reg);return in(0x225);}
int main(void) {
    reset();assert(mix(0x80)==2 && mix(0x81)==0x22 && !mix(0x82));
    dsp(0xe3);
    const char copyright[]="COPYRIGHT (C) CREATIVE TECHNOLOGY LTD, 1992.";
    for (unsigned i=0;i<sizeof(copyright);i++) assert(in(0x22a)==(unsigned)copyright[i]);
    sample(0x1000,-32768);sample(0x1002,32767);
    sample(0x1004,-1234);sample(0x1006,2345);
    dma(0x1000,4,0);play(0xb0,0x30,4);
    msdos_sound_render(output,2);
    assert(output[0]==-32768 && output[1]==32767);
    assert(output[2]==-1234 && output[3]==2345);
    assert(mix(0x82)==2 && msdos_sound_irq()==13);
    in(0x22e);assert(mix(0x82)==2); /* 8-bit ACK must not lose 16-bit IRQ */
    in(0x22f);assert(!mix(0x82));out(0x20,0x20);
    assert(in(0xd0)==2 && in(0xd0)==0);
    /* Signed/unsigned mono, independent DMA mask, pause/resume and auto-init. */
    reset();sample(0x1000,0);sample(0x1002,65535);
    dma(0x1000,2,1);play(0xb6,0,2);dsp(0xd5);
    msdos_sound_render(output,1);assert(!output[0] && !mix(0x82));
    dsp(0xd6);msdos_sound_render(output,2);
    assert(output[0]==-32768 && output[1]==-32768 && output[2]==32767);
    assert(mix(0x82)==2);in(0x22f);out(0x20,0x20);
    dsp(0xd9);msdos_sound_render(output,2);
    assert(mix(0x82)==2);in(0x22f);out(0x20,0x20);
    msdos_sound_render(output,2);assert(!mix(0x82));
    /* Channel 5 has a 128 KiB page, ignoring the low page-register bit. */
    reset();sample(0x1fffe,100);sample(0,200);
    dma(0x1fffe,2,0);play(0xb0,0x10,2);msdos_sound_render(output,2);
    assert(output[0]==100 && output[2]==200);
    reset();sample(0x1000,20000);dma(0x1000,1,1);play(0xb4,0x10,1);
    out(0x224,0x30);out(0x225,0);msdos_sound_render(output,1);
    assert(!output[0] && output[1]==20000);
    out(0x224,0);out(0x225,0);msdos_sound_render(output,1);
    assert(output[0]==20000 && output[1]==20000);
    reset();dsp(0xf2);dsp(0xf3);assert(mix(0x82)==3);
    in(0x22f);assert(mix(0x82)==1);in(0x22e);assert(!mix(0x82));
    reset();dma(0x50000,1,0);play(0xb0,0x10,1);
    msdos_sound_render(output,1);assert(msdos_sound_error()==255 && !output[0]);
    reset();play(0xbe,0,1);assert(msdos_sound_error()==0xbe); /* no recording */
    reset();ram[0x1000]=0x80;ram[0x1001]=0x7f;
    out(0x0c,0);out(2,0);out(2,0x10);out(0x83,0);
    out(3,1);out(3,0);out(0x0b,0x49);out(0x0a,1);
    play(0xc0,0x30,2);msdos_sound_render(output,1);
    assert(output[0]==-32768 && output[1]==32512 && mix(0x82)==1);
    msdos_sound_reset(MSDOS_SOUND_SB2,ram,sizeof(ram));dsp(0xb0);
    assert(msdos_sound_error()==0xb0 && in(0x225)==255 && in(0x22f)==255);
    puts("PASS: SB16 4.05 detection, DMA5 word/page/count, signed/unsigned stereo/mono, "
         "IRQ ACK separation, auto-init/stop/pause, mixer, safe bounds, SB2 isolation");
}
