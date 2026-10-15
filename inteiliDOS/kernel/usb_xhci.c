/* xHCI boot-keyboard driver: one direct root-port keyboard, polled events.
 * Intel xHCI 1.x register/TRB/context layouts. Identity-mapped 32-bit DMA;
 * a 64-bit CPU is not required. No hubs, MSI, above-4GiB BARs or hotplug.
 */
#include "usb_xhci.h"
#include "usb.h"
#include "pci.h"
#include "timer.h"
#include "memory.h"
#include <stdint.h>

#define RING_SIZE 64
#define TRB_TYPE(n) ((uint32_t)(n)<<10)
typedef struct { uint32_t lo, hi, size, flags; } trb_t;
typedef struct {
    trb_t trbs[RING_SIZE];
    unsigned index, cycle;
} ring_t;
static ring_t commands __attribute__((aligned(64)));
static ring_t control_ring __attribute__((aligned(64)));
static ring_t keyboard_ring __attribute__((aligned(64)));
static volatile trb_t events[RING_SIZE] __attribute__((aligned(64)));
static uint32_t erst[4] __attribute__((aligned(64)));
static uint64_t dcbaa[256] __attribute__((aligned(64)));
static uint32_t input_context[33*16] __attribute__((aligned(64)));
static uint32_t output_context[32*16] __attribute__((aligned(64)));
static uint64_t scratch_list[32] __attribute__((aligned(64)));
static uint8_t scratch_pages[32][4096] __attribute__((aligned(4096)));
static uint8_t data[512] __attribute__((aligned(64)));
static uint8_t report[8] __attribute__((aligned(64))), previous[8];
static uint32_t op, doorbell, interrupter;
static unsigned stride, event_index, event_cycle, slot, root_port, speed, dci;
static int ready;
static int dma_unsafe;

static uint32_t phys(const volatile void *p) { return (uint32_t)(uintptr_t)p; }
static uint32_t rd(uint32_t p) { return *(volatile uint32_t *)(uintptr_t)p; }
static void wr(uint32_t p, uint32_t v) {
    __asm__ volatile("":::"memory");
    *(volatile uint32_t *)(uintptr_t)p=v;
}
static void wr64(uint32_t p, uint32_t low) { wr(p,low); wr(p+4,0); }
static int wait_bits(uint32_t p, uint32_t mask, uint32_t wanted, uint32_t ms) {
    uint32_t start=timer_get_ticks();
    while ((rd(p)&mask)!=wanted) {
        if (timer_get_ticks()-start>=ms) return -1;
        __asm__ volatile("hlt");
    }
    return 0;
}
static void init_ring(ring_t *r) {
    kmemset(r,0,sizeof(*r));
    r->cycle=1;
    r->trbs[RING_SIZE-1]=(trb_t){phys(r->trbs),0,0,TRB_TYPE(6)|2};
}
static uint32_t enqueue(ring_t *r,uint32_t lo,uint32_t hi,uint32_t size,uint32_t flags) {
    volatile trb_t *t=&r->trbs[r->index];
    t->lo=lo; t->hi=hi; t->size=size;
    __asm__ volatile("":::"memory");
    t->flags=flags|r->cycle;
    uint32_t address=phys(t);
    if (++r->index==RING_SIZE-1) {
        /* Publish a cycle-matched Link TRB before toggling on wrap. */
        r->trbs[RING_SIZE-1].flags=TRB_TYPE(6)|2|r->cycle;
        r->index=0; r->cycle^=1;
    }
    return address;
}
static int next_event(trb_t *out) {
    volatile trb_t *e=&events[event_index];
    if ((e->flags&1)!=event_cycle) return 0;
    __asm__ volatile("":::"memory");
    *out=*e;
    if (++event_index==RING_SIZE) {event_index=0;event_cycle^=1;}
    /* Acknowledge EHB; event delivery is polled, IMAN.IE remains clear. */
    wr64(interrupter+0x18,phys(&events[event_index])|8);
    wr(interrupter,1);
    return 1;
}
static int wait_event(unsigned type,uint32_t cookie,trb_t *out) {
    uint32_t start=timer_get_ticks();
    while (timer_get_ticks()-start<1000) {
        trb_t e;
        if (!next_event(&e)) {__asm__ volatile("hlt");continue;}
        if (((e.flags>>10)&63)!=type || e.lo!=cookie || e.hi) continue;
        *out=e;
        unsigned completion=e.size>>24;
        return completion==1 || (type==32 && completion==13) ? 0 : -1;
    }
    return -1;
}
static int command(unsigned type,uint32_t pointer,uint32_t flags,trb_t *event) {
    uint32_t cookie=enqueue(&commands,pointer,0,0,TRB_TYPE(type)|flags);
    wr(doorbell,0);
    return wait_event(33,cookie,event);
}
static uint32_t *in_ctx(unsigned index) { return input_context+index*stride; }
static uint32_t *out_ctx(unsigned index) { return output_context+index*stride; }
static void slot_context(unsigned entries) {
    uint32_t *s=in_ctx(1);
    s[0]=(speed<<20)|(entries<<27);
    s[1]=root_port<<16;
}
static int control(uint8_t request_type,uint8_t request,uint16_t value,
                   uint16_t index,uint16_t length) {
    int in=!!(request_type&0x80);
    enqueue(&control_ring,request_type|((uint32_t)request<<8)|((uint32_t)value<<16),
            index|((uint32_t)length<<16),8,
            TRB_TYPE(2)|(1<<6)|(length ? (in ? 3u : 2u)<<16 : 0));
    if (length)
        enqueue(&control_ring,phys(data),0,length,TRB_TYPE(3)|(in ? 1u<<16 : 0));
    uint32_t cookie=enqueue(&control_ring,0,0,0,TRB_TYPE(4)|(1<<5)|
                           ((!length || !in) ? 1u<<16 : 0));
    wr(doorbell+slot*4,1);
    trb_t event;
    return wait_event(32,cookie,&event);
}
static int descriptor(unsigned type,unsigned length) {
    if (length>sizeof(data)) return -1;
    kmemset(data,0,sizeof(data));
    return control(0x80,6,(uint16_t)(type<<8),0,(uint16_t)length);
}
static void arm_report(void) {
    enqueue(&keyboard_ring,phys(report),0,8,TRB_TYPE(1)|(1<<5));
    wr(doorbell+slot*4,dci);
}
void usb_xhci_poll(void) {
    if (!ready) return;
    if ((rd(op+4)&4) || !(rd(op+0x400+(root_port-1)*16)&1)) {
        ready=0;
        return;
    }
    /* Bound interrupt-handler work; never wait, enumerate or print here. */
    for (unsigned n=0;n<16;n++) {
        trb_t e;
        if (!next_event(&e)) break;
        if (((e.flags>>10)&63)!=32 || (e.flags>>24)!=slot ||
            ((e.flags>>16)&31)!=dci) continue;
        unsigned code=e.size>>24;
        if (code!=1 && code!=13) {ready=0;return;}
        if (!(e.size&0xFFFFFF) && usb_process_boot_report(report,previous))
            kmemcpy(previous,report,8);
        arm_report();
    }
}

static int keyboard_on_port(unsigned port) {
    uint32_t p=op+0x400+(port-1)*16, value=rd(p);
    if (!(value&1)) return 0;
    /* Exclude all change bits and PED (writing PED=1 disables the port). */
    wr(p,(value&~(0x00FE0000u|2u))|(1u<<9));
    timer_sleep(100);
    value=rd(p);
    wr(p,(value&~(0x00FE0000u|2u))|(1u<<4));
    if (wait_bits(p,1u<<4,0,250) || !(rd(p)&2)) return 0;
    timer_sleep(20);
    root_port=port;speed=(rd(p)>>10)&15;
    if (speed<1 || speed>4) return 0;
    trb_t event;
    if (command(9,0,0,&event)) return -1;
    slot=event.flags>>24;
    if (slot!=1) return -1; /* CONFIG.MaxSlotsEn = 1 */
    init_ring(&control_ring);init_ring(&keyboard_ring);
    kmemset(input_context,0,sizeof(input_context));
    kmemset(output_context,0,sizeof(output_context));
    dcbaa[slot]=phys(output_context);
    in_ctx(0)[1]=3; /* add Slot and EP0 */
    slot_context(1);
    uint32_t *ep=in_ctx(2);
    unsigned mps=speed==4 ? 512 : speed==3 ? 64 : 8;
    ep[1]=(3<<1)|(4<<3)|(mps<<16);
    ep[2]=phys(control_ring.trbs)|1;
    ep[4]=8;
    if (command(11,phys(input_context),slot<<24,&event)) return -1;
    if (descriptor(1,8) || data[0]<18 || data[1]!=1) return -1;
    unsigned reported=speed==4 ? (data[7]==9 ? 512u : 0u) : data[7];
    if ((reported!=8 && reported!=16 && reported!=32 && reported!=64 && reported!=512) ||
        (speed==2 && reported!=8)) return -1;
    if (reported!=mps) {
        kmemset(input_context,0,sizeof(input_context));
        in_ctx(0)[1]=2;
        kmemcpy(in_ctx(2),out_ctx(1),stride*4);
        in_ctx(2)[1]=(in_ctx(2)[1]&0xFFFF)|(reported<<16);
        if (command(13,phys(input_context),slot<<24,&event)) return -1;
    }
    if (descriptor(1,18) || descriptor(2,9) || data[0]!=9 || data[1]!=2) return -1;
    unsigned length=data[2]|((unsigned)data[3]<<8);
    if (length<9 || length>sizeof(data) || descriptor(2,length)) return -1;
    usb_keyboard_descriptor_t info;
    if (usb_parse_boot_keyboard(data,length,&info)) return -1;
    if (control(0,9,info.configuration,0,0) ||
        control(0x21,11,0,info.interface,0)) return -1; /* boot protocol */
    /* SET_IDLE is advisory and not implemented by every keyboard. Do not
     * issue an optional request whose STALL would halt our EP0 context. */
    kmemset(input_context,0,sizeof(input_context));
    dci=info.endpoint*2+1;
    in_ctx(0)[1]=1u|(1u<<dci);
    slot_context(dci);
    ep=in_ctx(dci+1);
    unsigned interval;
    if (speed<=2) {
        interval=3;
        for (unsigned frames=1;frames<info.interval && interval<10;frames<<=1) interval++;
    } else {
        if (info.interval>16) return -1;
        interval=info.interval-1;
    }
    ep[0]=interval<<16;
    ep[1]=(3<<1)|(7<<3)|((uint32_t)info.packet_size<<16);
    ep[2]=phys(keyboard_ring.trbs)|1;
    ep[4]=8|((uint32_t)info.packet_size<<16);
    if (command(12,phys(input_context),slot<<24,&event)) return -1;
    kmemset(previous,0,sizeof(previous));
    ready=1;arm_report();
    return 1;
}

static int controller(const pci_device_t *device) {
    if ((device->bar[0]&1) || !(device->bar[0]&0xFFFFFFF0u) ||
        ((device->bar[0]&6)==4 && device->bar[1])) return 0;
    uint32_t base=device->bar[0]&0xFFFFFFF0u;
    pci_enable_busmaster(device);
    uint32_t cap=rd(base), hcs=rd(base+4), hcc=rd(base+0x10);
    unsigned ports=hcs>>24;
    if ((cap&255)<0x20 || !(hcs&255) || !ports) return 0;
    op=base+(cap&255);
    doorbell=base+(rd(base+0x14)&~3u);
    interrupter=base+(rd(base+0x18)&~31u)+0x20;
    stride=(hcc&4) ? 16 : 8;
    /* Respect BIOS ownership before stopping or resetting the controller. */
    uint32_t ext=base+((hcc>>16)*4);
    for (unsigned n=0;(hcc>>16) && n<64;n++) {
        uint32_t v=rd(ext);
        if ((v&255)==1) {
            wr(ext,v|(1u<<24));
            if (wait_bits(ext,1u<<16,0,1000)) return 0;
            wr(ext+4,0);
            break;
        }
        unsigned next=(v>>8)&255;
        if (!next) break;
        ext+=next*4;
    }
    wr(op,rd(op)&~1u);
    if (wait_bits(op+4,1,1,1000)) return 0;
    wr(op,2);
    if (wait_bits(op,2,0,1000) || wait_bits(op+4,1u<<11,0,1000) ||
        !(rd(op+8)&1)) return 0;
    unsigned pads=((rd(base+8)>>27)&31)|(((rd(base+8)>>21)&31)<<5);
    if (pads>32) return 0;
    init_ring(&commands);
    kmemset((void *)events,0,sizeof(events));
    kmemset(dcbaa,0,sizeof(dcbaa));
    event_index=0;event_cycle=1;slot=0;
    for (unsigned i=0;i<pads;i++) scratch_list[i]=phys(scratch_pages[i]);
    if (pads) dcbaa[0]=phys(scratch_list);
    wr64(op+0x30,phys(dcbaa));
    wr64(op+0x18,phys(commands.trbs)|1);
    wr(op+0x38,1);
    erst[0]=phys(events);erst[1]=0;erst[2]=RING_SIZE;erst[3]=0;
    wr(interrupter+8,1);wr64(interrupter+0x10,phys(erst));
    wr64(interrupter+0x18,phys(events));wr(interrupter,1);
    wr(op,1); /* Run; interrupt delivery disabled */
    if (wait_bits(op+4,1,0,1000)) return 0;
    for (unsigned port=1;port<=ports;port++) {
        int result=keyboard_on_port(port);
        if (result==1) return 1;
        if (slot) {
            trb_t event;
            if (command(10,0,slot<<24,&event)) break;
            slot=0;
        }
    }
    wr(op,0);
    if (wait_bits(op+4,1,1,1000)) dma_unsafe=1;
    return 0;
}
int usb_xhci_init(void) {
    ready=0;
    if (dma_unsafe) return 0; /* never reuse DMA buffers owned by a stuck HBA */
    pci_device_t current, next;
    if (pci_find_class(0x0C,0x03,&current)) return 0;
    do {
        if (current.prog_if==0x30 && controller(&current)) return 1;
        if (dma_unsafe) break;
        if (pci_find_class_after(0x0C,0x03,&current,&next)) break;
        current=next;
    } while (1);
    return 0;
}
