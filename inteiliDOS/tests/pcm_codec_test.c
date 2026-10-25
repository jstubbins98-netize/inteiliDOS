/* Exercise analog routing against a delayed/failing codec, without native I/O. */
#include <assert.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#define PCM_CODEC_TEST
static int verb(unsigned nid,unsigned code,unsigned value,uint32_t *response);
#include "../kernel/pcm.c"
#include "../kernel/msdos/sound.h"

static unsigned requested[4], delays, stuck, fail_verb, sleeps;
static unsigned pin_control, eapd, amps, bad_order;
static char messages[2048];
void timer_sleep(uint32_t ms) {sleeps+=ms;}
void vga_puts(const char *text) {strcat(messages,text);}
void vga_printf(const char *format,...) {
    va_list ap;va_start(ap,format);
    vsnprintf(messages+strlen(messages),sizeof(messages)-strlen(messages),format,ap);
    va_end(ap);
}
static int verb(unsigned n,unsigned code,unsigned value,uint32_t *response) {
    *response=0;
    if (code==fail_verb) return 0;
    if (code==0xf00) {
        if (n==0 && value==4) *response=0x10001;
        if (n==1 && value==5) *response=1;
        if (n==1 && value==4) *response=0x20002;
        if (value==9) {
            if (n==2) *response=(4u<<20)|(1u<<10)|4|8;
            if (n==3) *response=(1u<<10)|4|8;
        }
        if (n==2 && value==0x0c) *response=16|(1u<<16);
        if (n==2 && value==0x0e) *response=1;
        if (value==0x12) *response=0x1f;
    } else if (code==0x705) requested[n]=1;
    else if (code==0xf05) {
        assert(requested[n]);
        if (stuck || delays) {*response=0x30;if(delays) delays--;}
    } else if (code==0xf1c) *response=n==2?1u<<20:1u<<30;
    else if (code==0xf02) {*response=3;assert(requested[n]);}
    else if (code==0x707) pin_control=value;
    else if (code==0xf07) *response=pin_control;
    else if (code==0xf0c) *response=eapd;
    else if (code==0x70c) eapd=value;
    else if ((code&0xf00)==0x300) {
        amps++;
        if (!requested[n]) bad_order=1;
    }
    return 1;
}
static void reset(void) {
    memset(requested,0,sizeof(requested));delays=stuck=fail_verb=sleeps=0;
    pin_control=amps=bad_order=0;eapd=5;
    report=(typeof(report)){0};messages[0]=0;
}
static void silence(int16_t *output,unsigned frames) {
    memset(output,0,frames*4);
}
static void signal(int16_t *output,unsigned frames) {
    silence(output,frames);output[0]=-32768;output[1]=12345;
}
static uint8_t fake_mmio[0x200],guest_ram[65536];
static void sb_out(unsigned port,unsigned value) {
    assert(msdos_sound_out(port,value));
}
static void short_probe(void) {
    msdos_sound_reset(MSDOS_SOUND_SB16,guest_ram,sizeof(guest_ram));
    sb_out(0x21,0xdf);sb_out(0x0a,5);sb_out(0x0c,0);
    sb_out(0x83,0);sb_out(2,0);sb_out(2,0x10);
    sb_out(3,3);sb_out(3,0);sb_out(0x0b,0x49);sb_out(0x0a,1);
    sb_out(0x22c,0x40);sb_out(0x22c,0xa6); /* 11111 Hz */
    sb_out(0x22c,0x14);sb_out(0x22c,3);sb_out(0x22c,0);
    produce=msdos_sound_render;
    report=(typeof(report)){0};
    memset(samples,0x55,sizeof(samples));
}
static void test_partial_periods(void) {
    /* Mario/Miles tests the IRQ with four DMA bytes and the speaker muted.
     * It must complete inside a native 10 ms period, not at its end. */
    short_probe();
    mmio=(uint32_t)(uintptr_t)fake_mmio;stream=0x80;backend=2;cursor=0;
    wr(stream+4,18*4);w8(stream+3,0);
    pcm_poll();
    assert(msdos_sound_irq()==13 && cursor==18 && report.frames==18);
    assert(samples[0][36]==0x5555); /* Unconsumed samples were untouched. */
    w8(stream+3,0);wr(stream+4,24*4);pcm_poll();
    assert(report.frames==24 && cursor==24);
    assert(samples[0][48]==0x5555);
    /* Refill only consumed frames across the cyclic BDL boundary. */
    cursor=QUEUE*FRAMES-5;report.frames=0;
    w8(stream+3,0);wr(stream+4,3*4);pcm_poll();
    assert(cursor==3 && report.frames==8);
    short_probe();cursor=partial=0;tail=3;
    refill_ac97(0,FRAMES*2-35);
    assert(partial==17 && report.frames==17 && !msdos_sound_irq_pending());
    assert(samples[0][34]==0x5555); /* Do not overwrite a half-read frame. */
    refill_ac97(0,FRAMES*2-18*2);
    assert(msdos_sound_irq()==13 && partial==18 && report.frames==18);
    assert(samples[0][36]==0x5555 && tail==3);
    refill_ac97(1,FRAMES*2-6*2);
    assert(cursor==1 && partial==6 && tail==4 && report.frames==FRAMES+6);
    assert(samples[1][12]==0x5555);
    /* Descriptor 31 wraps to 0 without dropping or duplicating samples. */
    cursor=31;partial=FRAMES-5;tail=2;report.frames=0;
    refill_ac97(0,FRAMES*2-3*2);
    assert(cursor==0 && partial==3 && tail==3 && report.frames==8);
    backend=0;
}
int main(void) {
    reset();delays=3;
    assert(find_output() && pin==2 && dac==3);
    assert(sleeps==3 && amps==2 && !bad_order);
    assert(pin_control==0x40 && eapd==7); /* Preserve BTL and swap. */
    reset();stuck=1;assert(!find_output() && sleeps==100 && !amps);
    reset();fail_verb=0x705;assert(!find_output() && !amps);
    reset();fail_verb=0x3b0;assert(!find_output());
    reset();fail_verb=0x70c;assert(!find_output());
    reset();fail_verb=0xf07;assert(!find_output());
    reset();produce=silence;fill(0);assert(!report.peak && report.frames==480);
    report.output=2;report.advances=10;report.polls=20;pcm_report();
    assert(strstr(messages,"no non-silent samples") && !strstr(messages,"stalled"));
    messages[0]=0;produce=signal;fill(0);
    assert(report.peak==32768 && report.frames==960);
    pcm_report();assert(!strstr(messages,"no non-silent"));
    messages[0]=0;report.errors=0x10;pcm_report();assert(strstr(messages,"stalled"));
    messages[0]=0;report.errors=0;report.polls=report.advances=0;pcm_report();
    assert(strstr(messages,"output not exercised") && !strstr(messages,"stalled"));
    test_partial_periods();
    puts("PASS: delayed power, failed verbs, analog routing, EAPD preservation, "
         "silent/non-silent diagnostics, sub-period SB DMA probes and ring wraps");
}
