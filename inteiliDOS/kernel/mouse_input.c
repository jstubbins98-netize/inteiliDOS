/* Bounded 8042 transactions. Never forward guest mouse/controller commands. */
#include "mouse_input.h"
#include "keyboard.h"
#include "isr.h"
#include "usb_xhci.h"
#include "usb.h"
static int active, configured, usb_active;
static uint8_t saved_config, packet[3], used;
static volatile int movement_x, movement_y;
static volatile unsigned buttons;
static uint8_t inb(uint16_t p) {
    uint8_t v; __asm__ volatile("inb %1,%0":"=a"(v):"Nd"(p)); return v;
}
static void outb(uint16_t p,uint8_t v) {
    __asm__ volatile("outb %0,%1"::"a"(v),"Nd"(p));
}
static unsigned lock(void) {
    unsigned flags; __asm__ volatile("pushfl; popl %0; cli":"=r"(flags)::"memory");
    return flags;
}
static void unlock(unsigned flags) {
    __asm__ volatile("pushl %0; popfl"::"r"(flags):"memory","cc");
}
static int put(uint16_t port,uint8_t value) {
    for (unsigned i=0;i<100000;i++)
        if (!(inb(0x64)&2)) {outb(port,value);return 1;}
    return 0;
}
static void drain(void) {
    for (unsigned i=0;i<64;i++) {
        uint8_t s=inb(0x64);
        if (!(s&1)) break;
        keyboard_ps2_input(s,inb(0x60));
    }
}
static int config_read(uint8_t *value) {
    drain();
    if (!put(0x64,0x20)) return 0;
    for (unsigned i=0;i<100000;i++) {
        uint8_t s=inb(0x64);
        if (s&1) {
            uint8_t v=inb(0x60);
            if (!(s&0x20)) {*value=v;return 1;}
            mouse_input_byte(v);
        }
    }
    return 0;
}
static int command(uint8_t value) {
    for (unsigned retry=0;retry<3;retry++) {
        if (!put(0x64,0xd4) || !put(0x60,value)) return 0;
        for (unsigned i=0;i<100000;i++) {
            uint8_t s=inb(0x64);
            if (!(s&1)) continue;
            uint8_t v=inb(0x60);
            if (!(s&0x20)) {keyboard_ps2_input(s,v);continue;}
            if (v==0xfa) return 1;
            if (v==0xfe) break;
            mouse_input_byte(v);
        }
    }
    return 0;
}
void mouse_input_byte(uint8_t value) {
    if (!active) return;
    if (!used && !(value&8)) return;
    packet[used++]=value;
    if (used!=3) return;
    used=0;
    buttons=(packet[0]&3)|((packet[0]&4)); /* left, right, middle */
    if (packet[0]&0xc0) return; /* overflow: buttons still have meaning */
    int x=(int)packet[1]-((packet[0]&0x10)?256:0);
    int y=(int)packet[2]-((packet[0]&0x20)?256:0);
    if (movement_x+x>=-32767 && movement_x+x<=32767) movement_x+=x;
    if (movement_y-y>=-32767 && movement_y-y<=32767) movement_y-=y;
}
static void mouse_irq(registers_t *regs) {
    (void)regs;
    drain(); /* route keyboard bytes too; never consume them as mouse packets */
}
int mouse_input_start(void) {
    /* Enumeration may sleep; do not hold the PS/2 IRQ lock across USB work.
     * Never reset a controller/port already owned by keyboard or storage. */
    usb_active=usb_xhci_mouse_start();
    if (usb_active) return 1;
    unsigned flags=lock();
    if (usb_uhci_mouse_start()) {
        usb_active=2;
        unlock(flags);
        return 1;
    }
    active=configured=used=0;movement_x=movement_y=0;buttons=0;
    isr_register_handler(44,mouse_irq);
    int ok=inb(0x64)!=0xff && config_read(&saved_config);
    if (ok) {
        configured=1;
        ok=put(0x64,0xa8) && put(0x64,0x60) &&
           put(0x60,(saved_config|2)&~0x20) && command(0xf6) && command(0xf4);
    }
    active=ok;
    unlock(flags);
    if (!ok) mouse_input_stop();
    return ok;
}
void mouse_input_stop(void) {
    unsigned flags=lock();
    usb_xhci_mouse_stop();usb_uhci_mouse_stop();usb_active=0;
    active=0;used=0;
    if (configured) {
        command(0xf5);
        uint8_t current;
        if (config_read(&current) && put(0x64,0x60))
            put(0x60,(current&~0x22)|(saved_config&0x22));
    }
    configured=0;
    unlock(flags);
}
void mouse_input_poll(int *dx,int *dy,unsigned *state) {
    unsigned flags=lock();
    if (usb_active) {
        if (usb_active==1) usb_xhci_mouse_poll(dx,dy,state);
        else usb_uhci_mouse_poll(dx,dy,state);
        unlock(flags);
        return;
    }
    *dx=movement_x;*dy=movement_y;*state=buttons;
    movement_x=movement_y=0;
    unlock(flags);
}
