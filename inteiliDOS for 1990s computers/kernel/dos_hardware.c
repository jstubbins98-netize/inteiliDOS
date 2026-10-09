/* Narrow virtual legacy devices. Never forward PIC/PIT0 writes to hardware. */
#include "dos_internal.h"
#include "timer.h"

#define PIT_HZ 1193182u
static uint8_t masks[2], speaker_control, refresh_phase;
static uint8_t access[3], mode[3], write_phase[3], read_phase[3];
static uint8_t latch_pending[3];
static uint16_t low_byte[3], latched[3];
static uint32_t divisor[3], phase[3], fraction;
static int pending, in_service, used_speaker;

static void speaker_update(void) {
    if ((speaker_control&3) == 3 && speaker_get_volume()) {
        speaker_on_divisor(divisor[2]);
        used_speaker = 1;
    } else if (used_speaker) speaker_off();
}
void dos_hardware_init(void) {
    masks[0] = 0xB8; masks[1] = 0xFF;
    speaker_control = refresh_phase = 0;
    fraction = 0; pending = in_service = used_speaker = 0;
    for (unsigned i = 0; i < 3; i++) {
        access[i] = 3; mode[i] = 3;
        write_phase[i] = read_phase[i] = 0;
        latch_pending[i] = 0;
        low_byte[i] = latched[i] = 0;
        divisor[i] = 65536; phase[i] = 0;
    }
}
void dos_hardware_cleanup(void) {
    if (used_speaker) speaker_off();
}
void dos_hardware_tick(void) {
    fraction += PIT_HZ%1000;
    unsigned step = PIT_HZ/1000 + fraction/1000;
    fraction %= 1000;
    for (unsigned i = 0; i < 3; i++) {
        phase[i] += step;
        if (phase[i] >= divisor[i]) {
            phase[i] %= divisor[i];
            if (!i) pending = 1;
        }
    }
}
int dos_hardware_take_timer(void) {
    if (!pending || in_service || (masks[0]&1)) return 0;
    pending = 0; in_service = 1; return 1;
}
void dos_hardware_eoi(void) { in_service = 0; }

int dos_legacy_port(uint16_t port, int write, uint8_t *value) {
    if (port == 0x21 || port == 0xA1) {
        unsigned chip = port == 0xA1;
        if (write) masks[chip] = *value;
        else *value = masks[chip];
        return 1;
    }
    if ((port == 0x20 || port == 0xA0) && write && *value == 0x20) {
        if (port == 0x20) dos_hardware_eoi();
        return 1;
    }
    if (port == 0x61) {
        if (write) {
            speaker_control = *value&3; /* no parity/NMI/controller writes */
            speaker_update();
        } else {
            refresh_phase ^= 0x10;
            *value = speaker_control | refresh_phase |
                     (phase[2] < divisor[2]/2 ? 0x20 : 0);
        }
        return 1;
    }
    if (port == 0x43 && write) {
        unsigned channel = *value>>6, rw = (*value>>4)&3;
        if (channel == 1 || channel == 3 || (*value&1)) return 0;
        if (!rw) {
            if (!latch_pending[channel])
                latched[channel] = divisor[channel]-phase[channel];
            latch_pending[channel] = 1;
            read_phase[channel] = 0;
        } else {
            unsigned new_mode = (*value>>1)&7;
            if (new_mode > 5) new_mode -= 4;
            if (new_mode != 2 && new_mode != 3) return 0;
            access[channel] = rw; mode[channel] = new_mode;
            write_phase[channel] = read_phase[channel] = 0;
            latch_pending[channel] = 0;
        }
        return 1;
    }
    if (port == 0x40 || port == 0x42) {
        unsigned channel = port-0x40;
        if (!write) {
            if (!latch_pending[channel] && !read_phase[channel])
                latched[channel] = divisor[channel]-phase[channel];
            *value = access[channel] == 2 || read_phase[channel] ?
                     latched[channel]>>8 : latched[channel];
            if (access[channel] == 3) read_phase[channel] ^= 1;
            if (access[channel] != 3 || !read_phase[channel]) latch_pending[channel] = 0;
            return 1;
        }
        uint32_t count;
        if (access[channel] == 3 && !write_phase[channel]) {
            low_byte[channel] = *value;
            write_phase[channel] = 1; return 1;
        }
        count = access[channel] == 1 ? *value :
                access[channel] == 2 ? (unsigned)*value<<8 :
                low_byte[channel] | ((unsigned)*value<<8);
        if (!count) count = 65536;
        /* The monitor services virtual IRQ0 from a 1000 Hz host tick. */
        if (!channel && count < 1194) return 0;
        divisor[channel] = count; phase[channel] = 0;
        write_phase[channel] = 0;
        if (channel == 2) speaker_update();
        return 1;
    }
    return dos_video_port(port, write, value);
}
