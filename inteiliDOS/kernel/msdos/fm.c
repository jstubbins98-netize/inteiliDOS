/* Isolated AdLib/OPL timers and port aliases. MPL-2.0.
 * Synthesis: Nuked OPL3 (LGPL-2.1-or-later), see opl3/LICENSE.
 * No MPU-401, MIDI, native ISA ports or native timer programming. */
#include "fm.h"
#include "sound.h"
#include "opl3/opl3.h"
static opl3_chip chip;
static struct {
    unsigned model,address[2],fraction;
    uint8_t latch[2],control,status,wave_enable,waves[32],used;
    unsigned remaining[2];
} f;

void msdos_fm_reset(unsigned model) {
    f=(typeof(f)){0};f.model=model;
    f.address[1]=0x100;
    OPL3_Reset(&chip,48000);
}
/* A nominal 1us ISA bus access permits classic AdLib polling/delay probes
 * to complete even within one CPU slice, before the native audio poll.
 * Rendering also advances timers at 48kHz, including silent output fallback.
 * This is deterministic compatibility timing, not a cycle-accurate ISA bus.
 * It never advances DSP/DMA or raises IRQ5. OPL status IRQ is polled only. */
static void advance(unsigned us) {
    for (unsigned i=0;i<2;i++) if (f.control&(1u<<i)) {
        unsigned period=(256u-f.latch[i])*(i?320:80);
        if (us>=f.remaining[i]) {
            unsigned bit=i?0x20:0x40;
            if (!(f.control&bit)) f.status|=bit;
            f.remaining[i]=period-(us-f.remaining[i])%period;
        } else f.remaining[i]-=us;
    }
}
static int decode(unsigned p) {
    if (p>=0x388 && p<=0x38b) return p-0x388;
    if (p>=0x220 && p<=0x223) return p-0x220;
    if (p==0x228 || p==0x229) return p-0x228;
    return -1;
}
int msdos_fm_in(unsigned p,uint8_t *v) {
    int port=decode(p);
    if (port<0) return 0;
    *v=255;
    if (!f.model || (port>=2 && f.model!=MSDOS_SOUND_SB16)) return 1;
    advance(1);
    if (!port) *v=f.status|(f.status?0x80:0)|
        (f.model==MSDOS_SOUND_SB2?6:0);
    return 1;
}
int msdos_fm_out(unsigned p,uint8_t v) {
    int port=decode(p);
    if (port<0) return 0;
    if (!f.model || (port>=2 && f.model!=MSDOS_SOUND_SB16)) return 1;
    advance(1);
    unsigned bank=(unsigned)port>>1;
    if (!(port&1)) {
        f.address[bank]=v|(bank<<8);return 1;
    }
    unsigned reg=f.address[bank];
    if (reg==2 || reg==3) {f.latch[reg-2]=v;return 1;}
    if (reg==4) {
        if (v&0x80) {f.status=0;return 1;}
        f.status&=~(v&0x60);
        for (unsigned i=0;i<2;i++)
            if ((v&~f.control)&(1u<<i))
                f.remaining[i]=(256u-f.latch[i])*(i?320:80);
        f.control=v&0x63;return 1;
    }
    if (f.model==MSDOS_SOUND_SB2) {
        /* YM3812 has four waveforms gated by register 01h, bit 5.
         * YMF262 does not have this gate; Nuked masks its waveforms by mode. */
        if (reg==1) {
            f.wave_enable=v&0x20;
            for (unsigned i=0;i<32;i++)
                OPL3_WriteReg(&chip,0xe0+i,f.wave_enable?f.waves[i]:0);
        }
        if (reg>=0xe0 && reg<=0xff) {
            f.waves[reg&31]=v&3;v=f.wave_enable?v&3:0;
        }
    }
    /* Immediate writes avoid a queued key-on outliving a DSP reset or exit.
     * Guest bus delays are modeled above; native output stays bounded. */
    OPL3_WriteReg(&chip,reg,v);f.used=1;
    return 1;
}
void msdos_fm_sample(int16_t stereo[2]) {
    stereo[0]=stereo[1]=0;
    if (!f.model) return;
    f.fraction+=1000000;
    advance(f.fraction/48000);f.fraction%=48000;
    if (f.used) OPL3_GenerateResampled(&chip,stereo);
}
