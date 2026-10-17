/* Host tests exercise the real PS/2 decoder, shared queue, USB translator,
 * and DOS BIOS encoding, without executing port I/O. */
#include <assert.h>
#include <stdio.h>
#include "../kernel/keyboard.c"
#include "../kernel/usb.h"
#include "../kernel/msdos/keyboard_input.h"

static keyboard_event_t next(void) {
    keyboard_event_t e;
    assert(keyboard_poll_event(&e));
    return e;
}
static void expect(uint8_t c, uint8_t scan, uint8_t mods) {
    keyboard_event_t e=next();
    assert(e.character==c && e.scancode==scan && e.modifiers==mods);
}
static void bios(uint8_t c,uint8_t scan,uint8_t mods,unsigned expected) {
    keyboard_event_t e={c,scan,mods,0};
    unsigned physical=scan;
    if (!physical && c>=KEY_F1 && c<=KEY_F12)
        physical=c-KEY_F1<10 ? 0x3b+c-KEY_F1 : 0x57+c-KEY_F11;
    assert((unsigned)msdos_keyboard_encode(&e)==
           (expected | msdos_keyboard_state(&e) | (physical<<24)));
}
int main(void) {
    /* Left/right releases must be independent, including E0 right Ctrl/Alt. */
    keyboard_scancode(0x2a);expect(0,0,2);
    keyboard_scancode(0x36);expect(0,0,0x22);
    keyboard_scancode(0xaa);expect(0,0,0x20);
    keyboard_scancode(0x3b);expect(KEY_F1,0x3b,0x20);
    keyboard_scancode(0xb6);expect(0,0,0);
    keyboard_scancode(0xe0);keyboard_scancode(0x1d);expect(0,0,0x10);
    keyboard_scancode(0xe0);keyboard_scancode(0x38);expect(0,0,0x50);
    keyboard_scancode(0x1e);expect('a',0x1e,0x50);
    keyboard_scancode(0xe0);keyboard_scancode(0x9d);expect(0,0,0x40);
    keyboard_scancode(0xe0);keyboard_scancode(0xb8);expect(0,0,0);
    /* PrintScreen fake shifts and Pause's embedded Ctrl must not stick. */
    const uint8_t fake[]={0xe0,0x2a,0xe0,0xaa,0xe1,0x1d,0x45,0xe1,0x9d,0xc5};
    for (unsigned i=0;i<sizeof(fake);i++) keyboard_scancode(fake[i]);
    assert(keyboard_poll()==-1);
    /* Legacy reads skip state changes; PS/2 Ctrl typing stays unchanged. */
    keyboard_scancode(0x1d);keyboard_scancode(0x2e);keyboard_scancode(0x9d);
    assert(keyboard_poll()=='c' && keyboard_poll()==-1);
    keyboard_inject('X');assert(keyboard_poll()=='X');
    keyboard_scancode(0x2a);
    assert(keyboard_poll()==-1);
    keyboard_scancode(0x3c);assert(keyboard_poll()==KEY_F2);
    keyboard_scancode(0xaa);assert(keyboard_poll()==-1);

    uint8_t zero[8]={0},shift[8]={2},f1[8]={2,0,0x3a};
    assert(usb_process_boot_report(shift,zero));expect(0,0,2);
    assert(usb_process_boot_report(f1,shift));expect(KEY_F1,0x3b,2);
    assert(usb_process_boot_report(f1,f1));assert(keyboard_poll()==-1);
    assert(usb_process_boot_report(zero,f1));expect(0,0,0);
    uint8_t ctrlc[8]={1,0,6};
    assert(usb_process_boot_report(ctrlc,zero));expect(0,0,1);expect(3,0x2e,1);
    assert(usb_process_boot_report(zero,ctrlc));expect(0,0,0);
    /* Both physical sources contribute to a snapshot without clearing peers. */
    keyboard_scancode(0x2a);expect(0,0,2);
    uint8_t alt[8]={0x40};
    assert(usb_process_boot_report(alt,zero));expect(0,0,0x42);
    assert(usb_process_boot_report(zero,alt));expect(0,0,2);
    keyboard_scancode(0xaa);expect(0,0,0);
    for (unsigned i=0;i<12;i++) {
        bios(KEY_F1+i,0,0,(i<10 ? 0x3b+i : 0x85+i-10)<<8);
        bios(KEY_F1+i,0,2,(i<10 ? 0x54+i : 0x87+i-10)<<8);
        bios(KEY_F1+i,0,1,(i<10 ? 0x5e + i : 0x89+i-10)<<8);
        bios(KEY_F1+i,0,4,(i<10 ? 0x68+i : 0x8b+i-10)<<8);
    }
    bios('h',0x23,1,0x2308);bios(8,0x23,1,0x2308);
    bios('I',0x17,3,0x1709);bios(13,0x32,1,0x320d);
    bios('@',3,3,0x0300);bios(' ',0x39,1,0x3900);
    bios('\t',0x0f,2,0x0f00);bios(KEY_LEFT,0x4b,1,0x7300);
    bios(KEY_F12,0x58,7,0x8a00);
    keyboard_event_t caps={'a',0x1e,2,1};
    assert((msdos_keyboard_encode(&caps)&0xffff)==0x1e61);
    puts("PASS: modifier snapshots, independent sides, PS/2 E0/Pause/PrintScreen, "
         "USB held/released reports, mixed sources, legacy queue and BIOS encoding");
}
