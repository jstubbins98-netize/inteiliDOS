/* Host regression: FM is independent of DSP speaker/reset, DMA and IRQ5. */
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../kernel/msdos/sound.h"
static uint8_t ram[0x20000];
static int16_t output[48000*2],reference[48000*2];
static void out(unsigned p,unsigned v) {assert(msdos_sound_out(p,v));}
static unsigned in(unsigned p) {uint8_t v;assert(msdos_sound_in(p,&v));return v;}
static void reg(unsigned base,unsigned r,unsigned v) {out(base,r);out(base+1,v);}
static void reset(unsigned model) {msdos_sound_reset(model,ram,sizeof(ram));}
static void render(unsigned n) {msdos_sound_render(output,n);}
static void note(unsigned base) {
    reg(base,1,0x20);
    reg(base,0x20,0x21);reg(base,0x23,0x21);
    reg(base,0x40,0x3f);reg(base,0x43,0);
    reg(base,0x60,0xf0);reg(base,0x63,0xf0);
    reg(base,0x80,0x0f);reg(base,0x83,0x0f);
    reg(base,0xc0,0x31); /* additive, carrier only, both OPL3 sides */
    reg(base,0xa0,0x44);reg(base,0xb0,0x32); /* ~440 Hz */
}
static void audible(void) {
    unsigned count=0;
    for (unsigned i=0;i<96000;i++) if (abs(output[i])>100) count++;
    assert(count>48000);
}
static void silent_at(unsigned n,unsigned line) {
    for (unsigned i=0;i<n*2;i++) if (output[i]) {
        fprintf(stderr,"nonzero silence at test line %u sample %u: %d\n",line,i,output[i]);
        abort();
    }
}
#define silent(n) silent_at(n,__LINE__)
static void mix(unsigned r,unsigned v) {out(0x224,r);out(0x225,v);}
static void timers(unsigned model) {
    reset(model);
    unsigned idle=model==MSDOS_SOUND_SB2?6:0;
    assert(in(0x388)==idle && in(0x389)==255);
    reg(0x388,4,0x60);reg(0x388,4,0x80);
    assert(in(0x388)==idle);
    reg(0x388,2,255);reg(0x388,4,0x21); /* timer 1, timer 2 masked */
    assert(in(0x388)==idle);
    for (unsigned i=0;i<200;i++) in(0x388);
    assert(in(0x388)==(idle|0xc0));
    assert(msdos_sound_irq()==-1); /* no FM -> DSP interrupt */
    reg(0x388,4,0x80);assert(in(0x388)==idle);
    render(48);assert(in(0x388)==(idle|0xc0)); /* reset preserves running */
    reg(0x388,4,0x60);render(48);assert(in(0x388)==idle);
    reg(0x388,3,255);reg(0x388,4,0x42);
    render(12);assert(in(0x388)==idle); /* before 320us */
    render(12);assert(in(0x388)==(idle|0xa0));
    reg(0x388,4,0x62);render(48);assert(in(0x388)==idle); /* masked */
    reg(0x388,4,0x60);reg(0x388,4,0x80);
    render(480);assert(in(0x388)==idle);
    /* Shared address latch across SB and AdLib aliases. */
    out(0x220,2);out(0x389,255);
    out(0x228,4);out(0x221,1);
    render(48);assert(in(0x228)==(idle|0xc0) && in(0x220)==(idle|0xc0));
}
int main(void) {
    for (unsigned model=1;model<=2;model++) {
        timers(model);
        reset(model);note(0x388);render(48000);audible();
        memcpy(reference,output,sizeof(output));
        reset(model);note(0x220); /* FM must survive DSP reset and D3 */
        out(0x226,1);out(0x226,0);assert(in(0x22a)==0xaa);
        out(0x22c,0xd3);render(48000);
        assert(!memcmp(reference,output,sizeof(output)));
        reset(model);note(0x228);
        out(0x22c,0xd1);out(0x22c,0x10);out(0x22c,144); /* +4096 */
        render(48000);
        for (unsigned i=0;i<96000;i++) assert(output[i]==reference[i]+4096);
        reg(0x388,0xb0,0x12);render(48000);render(48000);
        out(0x22c,0xd3);render(48000);
        /* Nuked's logarithmic DAC has a few LSBs of quantization at release. */
        for (unsigned i=0;i<96000;i++) assert(abs(output[i])<4);
        /* An FM note cannot shift the four-byte digital IRQ probe. */
        reset(model);note(0x388);
        out(0x0c,0);out(2,0);out(2,0x10);out(0x83,0);
        out(3,3);out(3,0);out(0x0b,0x49);out(0x0a,1);
        out(0x21,0xdf);
        out(0x22c,0x40);out(0x22c,165); /* ~11kHz */
        out(0x22c,0x14);out(0x22c,3);out(0x22c,0);
        render(17);assert(msdos_sound_irq()==-1);
        render(1);assert(msdos_sound_irq()==13);
        assert(in(8)==2);
        in(0x22e);out(0x20,0x20);assert(msdos_sound_irq()==-1);
        msdos_sound_stop();render(48000);silent(48000);
        assert(in(0x388)==255 && msdos_sound_irq()==-1);
        assert(msdos_sound_model()==model); /* next-session preference retained */
        reset(model);render(48000);silent(48000);
        note(0x388);render(48000);audible();
    }
    reset(MSDOS_SOUND_SB16);note(0x388);mix(0x34,0);mix(0x35,0);
    render(48000);silent(48000);
    mix(0x26,0xff);render(48000);audible();
    mix(0x30,0);mix(0x31,0);render(48000);silent(48000);
    mix(0,0);render(48000);audible();
    /* Independent OPL3 bank, stereo panning and OPL2 bank rejection. */
    reset(MSDOS_SOUND_SB16);reg(0x38a,5,1);note(0x38a);
    reg(0x38a,0xc0,0x11);render(48000);
    unsigned loud=0;
    for (unsigned i=100;i<48000;i++) {
        loud+=abs(output[i*2])>100;assert(!output[i*2+1]);
    }
    assert(loud>24000);
    reset(MSDOS_SOUND_SB2);reg(0x38a,5,1);note(0x222);
    render(48000);silent(48000);assert(in(0x38a)==255);
    /* Saturation, not signed wrap when PCM and music exceed int16 range. */
    reset(MSDOS_SOUND_SB16);note(0x388);
    out(0x22c,0xd1);out(0x22c,0x10);out(0x22c,255);render(48000);
    unsigned clipped=0;
    for (unsigned i=0;i<96000;i++) {assert(output[i]>20000);clipped+=output[i]==32767;}
    assert(clipped>1000);
    reset(MSDOS_SOUND_OFF);
    note(0x388);note(0x220);note(0x38a);
    for (unsigned p=0x388;p<=0x38b;p++) assert(in(p)==255);
    out(0x226,1);out(0x226,0);render(48000);silent(48000);
    assert(in(0x22a)==255 && msdos_sound_irq()==-1);
    uint8_t value;
    assert(!msdos_sound_in(0x330,&value)); /* no MPU-401 */
    assert(!msdos_sound_out(0x331,0xff));
    puts("PASS: OPL2/OPL3 detection/timers/aliases, music + PCM, DSP reset/speaker "
         "isolation, short DMA/IRQ timing, FM/master mixer, stereo, clipping, off, stop/re-entry");
}
