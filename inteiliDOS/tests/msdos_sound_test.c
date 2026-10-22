#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "../kernel/msdos/sound.h"
static uint8_t ram[0x20000];
static int16_t output[48000*2];
static void out(unsigned p,unsigned v) {assert(msdos_sound_out(p,v));}
static unsigned in(unsigned p) {uint8_t v;assert(msdos_sound_in(p,&v));return v;}
static void dsp(unsigned c) {out(0x22c,c);}
static void reset(void) {
    msdos_sound_reset(1,ram,sizeof(ram));
    out(0x226,1);out(0x226,0);assert(in(0x22e)==128 && in(0x22a)==0xaa);
}
static void dma(unsigned address,unsigned count,int automatic) {
    out(0x0a,5);out(0x0c,0);out(2,address);out(2,address>>8);
    out(0x83,address>>16);out(0x0c,0);out(3,count-1);out(3,(count-1)>>8);
    out(0x0b,automatic?0x59:0x49);out(0x0a,1);
}
int main(void) {
    msdos_sound_reset(0,ram,sizeof(ram));
    out(0x226,1);out(0x226,0);
    assert(in(0x22a)==255 && in(0x22e)==255 && msdos_sound_irq()==-1);
    msdos_sound_render(output,480);
    for (unsigned i=0;i<960;i++) assert(!output[i]);
    reset();dsp(0xe1);assert(in(0x22a)==2 && in(0x22a)==1);
    dsp(0xe0);dsp(0x3c);assert(in(0x22a)==0xc3);
    dsp(0xe4);dsp(0x57);dsp(0xe8);assert(in(0x22a)==0x57);
    /* Hardware-only DMA detection: E2h is not PCM, does not raise IRQ5,
     * waits for channel 1 to be armed, and resets its accumulator per DSP reset. */
    reset();ram[0x100]=0xcc;
    dsp(0xe2);dsp(0);
    dma(0x100,1,0);assert(ram[0x100]==0xcc); /* wrong transfer direction */
    out(0x0b,0x44);assert(ram[0x100]==0xcc); /* channel 0 is not our card */
    out(0x0b,0x45);assert(ram[0x100]==0x40);
    assert(in(3)==255 && in(3)==255 && in(8)==2 && !msdos_sound_error());
    out(0x21,0xdf);assert(msdos_sound_irq()==-1);
    dma(0x101,1,0);out(0x0b,0x45);
    dsp(0xe2);dsp(255);assert(ram[0x101]==0x9a);
    reset();dma(0x102,1,0);out(0x0b,0x45);dsp(0xe2);dsp(0);
    assert(ram[0x102]==0x40);
    reset();dma(0xff0000,1,0);out(0x0b,0x45);dsp(0xe2);dsp(0);
    assert(msdos_sound_error()==255);
    reset();dsp(0xe2);dsp(0);out(0x226,1);out(0x226,0);
    ram[0x100]=0xcc;dma(0x100,1,0);out(0x0b,0x45);
    assert(ram[0x100]==0xcc); /* no pending probe leaks across reset */
    reset();
    dsp(0xd1);dsp(0x10);dsp(255);msdos_sound_render(output,480);
    assert(output[0]==32512 && output[1]==32512);
    dsp(0xd3);msdos_sound_render(output,480);assert(!output[0]);
    reset();dma(0xfffe,4,0);out(0x21,0xdf);
    ram[0xfffe]=0;ram[0xffff]=64;ram[0]=192;ram[1]=255;
    dsp(0xd1);dsp(0x40);dsp(131); /* 8 kHz exactly */
    dsp(0x14);dsp(3);dsp(0);
    msdos_sound_render(output,24);
    assert(output[5*2]==-32768 && output[11*2]==-16384 &&
           output[17*2]==16384 && output[23*2]==32512);
    assert(msdos_sound_irq()==13 && msdos_sound_irq()==-1);
    assert(in(8)==2 && in(8)==0);
    in(0x22e);out(0x20,0x20);
    /* Pause/continue, auto-init reload, block interrupts and mask gating. */
    reset();dma(0x100,4,1);ram[0x100]=255;
    dsp(0xd1);dsp(0x40);dsp(131);dsp(0x48);dsp(3);dsp(0);
    dsp(0x1c);dsp(0xd0);msdos_sound_render(output,48);assert(!output[0]);
    dsp(0xd4);msdos_sound_render(output,24);assert(msdos_sound_irq()==-1);
    out(0x21,0xdf);assert(msdos_sound_irq()==13);
    in(0x22e);out(0x20,0x20);msdos_sound_render(output,24);
    assert(msdos_sound_irq()==13);
    in(0x22e);out(0x20,0x20);dsp(0xda);
    msdos_sound_render(output,24);assert(msdos_sound_irq()==13);
    in(0x22e);out(0x20,0x20);
    msdos_sound_render(output,48);assert(msdos_sound_irq()==-1);
    /* Invalid guest DMA never becomes a native pointer. */
    reset();dma(0xff0000,16,0);dsp(0x14);dsp(15);dsp(0);
    msdos_sound_render(output,480);assert(msdos_sound_error()==255);
    reset();dsp(0xf2);out(0x21,0xdf);assert(msdos_sound_irq()==13);
    reset();out(0x20,255);out(0x21,255);
    out(0x21,0xdf);dsp(0xf2);assert(msdos_sound_irq()==13);
    reset();out(0x20,0x13);out(0x21,0x50);out(0x21,1);
    out(0x21,0xdf);dsp(0xf2);assert(msdos_sound_irq()==0x55);
    reset();out(0x21,0xdf);assert(msdos_sound_irq()==-1);
    puts("PASS: disabled device, reset/version, E2 DMA detection, DAC, single/auto DMA, 64K wrap, "
         "IRQ/PIC acknowledgement, pause/continue/exit, invalid DMA, reset/re-entry");
}
