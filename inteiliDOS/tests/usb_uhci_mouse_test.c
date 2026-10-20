/* Actual UHCI schedule/session code, without privileged I/O. QEMU covers DMA,
 * descriptor enumeration and real DOS; this model covers exceptional reports. */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#define USB_UHCI_TEST
#include "../kernel/usb.c"

static uhci_controller_t model;
static unsigned ticks, writes, injected;
static uint16_t ports[2];
static uint16_t command, status;
static int refuse_halt;
static uint16_t inw(uint16_t port) {
    if (port==model.base+UHCI_USBCMD) return command;
    if (port==model.base+UHCI_USBSTS) return status;
    assert(port==model.base+UHCI_PORTSC1 || port==model.base+UHCI_PORTSC2);
    return ports[(port-model.base-UHCI_PORTSC1)/2];
}
static void outw(uint16_t p,uint16_t v) {
    writes++;
    assert(p==model.base+UHCI_USBCMD);
    command=v;
    if (v&UHCI_CMD_RS) status=0;
    else if (!refuse_halt) status=UHCI_STS_HCH;
}
static void outb(uint16_t p,uint8_t v) {(void)p;(void)v;writes++;}
static void outl(uint16_t p,uint32_t v) {(void)p;(void)v;writes++;}
static void io_wait(void) {ticks++;}
uint32_t timer_get_ticks(void) {return ticks;}
void keyboard_usb_modifiers(uint8_t m,uint8_t c) {(void)m;(void)c;}
void keyboard_current_event(keyboard_event_t *e) {memset(e,0,sizeof(*e));}
void keyboard_inject_event(uint8_t c,uint8_t s,uint8_t m,uint8_t caps) {
    (void)s;(void)m;(void)caps;
    assert(c=='a');injected++;
}

static uhci_hid_t *fresh(unsigned mouse_port) {
    memset(&model,0,sizeof(model));model.base=0x100;
    ticks=writes=injected=0;mouse_x=mouse_y=0;mouse_buttons=0;mouse_active=0;
    refuse_halt=0;command=UHCI_CMD_RS|UHCI_CMD_MAXP;status=0;
    mouse_controller=&model;mouse_device=&model.hid[mouse_port];
    ports[0]=ports[1]=UHCI_PORT_CCS|UHCI_PORT_EN;
    uhci_arm_intr_frames(&model);
    for (unsigned i=0;i<2;i++) {
        uhci_hid_t *h=&model.hid[i];
        h->port=i;h->protocol=i==mouse_port?2:1;
        h->address=i+1;h->endpoint=1;h->interval=10;h->lowspeed=1;h->ready=1;
        h->td.status=TD_STATUS_ACTIVE(1);h->td.link=LP_T;
        h->qh.element=(uint32_t)(uintptr_t)&h->td;
    }
    return mouse_device;
}
static void report(uhci_hid_t *h,unsigned length,uint8_t b,int8_t x,int8_t y) {
    h->report[0]=b;h->report[1]=(uint8_t)x;h->report[2]=(uint8_t)y;
    h->td.status=(length-1)&0x7FF;h->qh.element=LP_T;
    ticks+=10;
    uhci_poll(&model,h);
}
static void poll(int x,int y,unsigned b) {
    int dx,dy;unsigned buttons;
    usb_uhci_mouse_poll(&dx,&dy,&buttons);
    assert(dx==x && dy==y && buttons==b);
}
int main(void) {
    for (unsigned p=0;p<2;p++) {
        uhci_hid_t *h=fresh(p),*k=&model.hid[!p];
        uint32_t control_link=model.control.link;
        uint32_t keyboard_link=k->qh.link;
        uint32_t keyboard_td=k->qh.element;
        for (unsigned i=0;i<1024;i++)
            assert(model.frames[i]==((uint32_t)(uintptr_t)&model.control|LP_Q));
        assert(model.control.link==((uint32_t)(uintptr_t)&model.hid[0].qh|LP_Q));
        assert(model.hid[0].qh.link==((uint32_t)(uintptr_t)&model.hid[1].qh|LP_Q));
        assert(model.hid[1].qh.link==LP_T);
        report(h,3,7,100,-100); /* inactive input is drained, not delivered */
        assert(usb_uhci_mouse_start());poll(0,0,0);
        report(h,3,5,-128,127);poll(-128,127,5);poll(0,0,5);
        report(h,2,0,50,50);poll(0,0,5); /* short report cannot release buttons */
        report(h,0,0,50,50);poll(0,0,5); /* empty report cannot reuse DMA bytes */
        unsigned toggle=h->toggle;
        h->td.status=TD_STALLED;ticks+=10;
        uhci_poll(&model,h);
        assert(h->toggle==toggle && (h->td.status&TD_ACTIVE));
        poll(0,0,5);
        mouse_x=32760;mouse_y=-32760;
        report(h,3,2,127,-128);poll(32767,-32767,2);
        usb_uhci_mouse_stop();poll(0,0,0);
        report(h,3,7,50,50);assert(usb_uhci_mouse_start());poll(0,0,0);
        /* A just-completed report behind the interval gate must not leak
         * into a new DOS session. */
        usb_uhci_mouse_stop();
        h->report[0]=7;h->report[1]=50;h->report[2]=50;
        h->td.status=2;
        assert(usb_uhci_mouse_start());poll(0,0,0);
        assert(model.control.link==control_link && k->qh.link==keyboard_link &&
               k->qh.element==keyboard_td);
        report(k,8,0,0,4);assert(injected==1); /* keyboard remains operational */
        report(h,3,7,50,50);
        ports[p]|=UHCI_PORT_CSC;ticks+=10;poll(0,0,0);
        assert(!h->ready && h->qh.element==LP_T);
        assert(!usb_uhci_mouse_start()); /* replacement requires reboot */
        ports[p]=UHCI_PORT_CCS|UHCI_PORT_EN;
        assert(!usb_uhci_mouse_start());
        assert(k->ready && !writes); /* no port/controller writes at start/stop/poll */
    }
    uhci_hid_t *h=fresh(0);
    uint32_t element=model.hid[1].qh.element;
    model.control.element=0x1230;
    assert(!uhci_retire_control(&model));
    assert(model.control.element==LP_T && !model.unsafe && writes==2);
    assert(command==(UHCI_CMD_RS|UHCI_CMD_MAXP) &&
           model.hid[1].qh.element==element && h->ready); /* no port reset */
    refuse_halt=1;status=0;model.control.element=0x1230;
    assert(uhci_retire_control(&model)==-1 && ticks==50);
    assert(model.unsafe && !h->ready && !model.hid[1].ready);
    assert(model.control.element==0x1230); /* unretired DMA stays pinned */
    assert(!usb_uhci_mouse_start());
    puts("PASS: UHCI dual-root schedule, both port orders, signed movement/3 buttons, "
         "short/empty/error reports, saturation, stale session input, unplug/replacement, "
         "keyboard preservation, zero session hardware writes, control halt/timeout quarantine");
}
