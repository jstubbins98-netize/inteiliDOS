/* Shared xHCI driver: boot keyboard and up to four direct-root BOT drives.
 * Intel xHCI 1.x register/TRB/context layouts. Identity-mapped 32-bit DMA;
 * a 64-bit CPU is not required. No hubs, MSI or above-4GiB BARs.
 * Storage rescans never reset the controller or interrupt a live keyboard.
 */
#include "usb_xhci.h"
#include "usb_bot.h"
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
} __attribute__((aligned(1024))) ring_t; /* TRB segment cannot cross 64 KiB */
static ring_t commands;
typedef struct {
    ring_t control, in, out;
    uint32_t input[33*16], output[32*16];
    unsigned slot_id, port, rate, in_dci, out_dci;
    unsigned used, storage, failed;
} __attribute__((aligned(64))) device_t;
static device_t devices[5] __attribute__((aligned(64)));
static device_t *current, *keyboard_device, *disks[4];
#define control_ring (current->control)
#define keyboard_ring (current->in)
#define input_context (current->input)
#define output_context (current->output)
#define slot (current->slot_id)
#define root_port (current->port)
#define speed (current->rate)
#define dci (current->in_dci)
static volatile trb_t events[RING_SIZE] __attribute__((aligned(1024)));
static uint32_t erst[4] __attribute__((aligned(64)));
static uint64_t dcbaa[256] __attribute__((aligned(64)));
static uint64_t scratch_list[32] __attribute__((aligned(64)));
static uint8_t scratch_pages[32][4096] __attribute__((aligned(4096)));
static uint8_t data[512] __attribute__((aligned(64)));
static uint8_t report[8] __attribute__((aligned(64))), previous[8];
static uint8_t bulk_data[512] __attribute__((aligned(512)));
static uint32_t op, doorbell, interrupter;
static unsigned stride, event_index, event_cycle, max_devices, ports;
static int ready;
static int dma_unsafe;
static int initialized, running;
static volatile int busy;
static uint32_t storage_tag;
static void keyboard_event(const trb_t *e);
static usb_xhci_info_t info = {.status="USB controller has not been initialized"};
const usb_xhci_info_t *usb_xhci_info(void) { return &info; }

static uint32_t phys(const volatile void *p) { return (uint32_t)(uintptr_t)p; }
#ifdef USB_XHCI_TEST
#include "../tests/usb_xhci_test_io.h"
#else
static uint32_t rd(uint32_t p) { return *(volatile uint32_t *)(uintptr_t)p; }
static void wr(uint32_t p, uint32_t v) {
    __asm__ volatile("":::"memory");
    *(volatile uint32_t *)(uintptr_t)p=v;
}
static void idle(void) { __asm__ volatile("hlt"); }
#endif
static void wr64(uint32_t p, uint32_t low) { wr(p,low); wr(p+4,0); }
static int wait_bits(uint32_t p, uint32_t mask, uint32_t wanted, uint32_t ms) {
    uint32_t start=timer_get_ticks();
    while ((rd(p)&mask)!=wanted) {
        if (timer_get_ticks()-start>=ms) return -1;
        idle();
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
        if (!next_event(&e)) {idle();continue;}
        if (((e.flags>>10)&63)!=type || e.lo!=cookie || e.hi) {
            keyboard_event(&e);
            continue;
        }
        *out=e;
        unsigned completion=e.size>>24;
        if (completion!=1 && !(type==32 && completion==13)) {
            info.completion_code=completion;info.event_type=type;
            info.status=type==33 ? "Controller rejected an xHCI command" :
                                  "USB device transfer failed";
        }
        return completion==1 || (type==32 && completion==13) ? 0 : -1;
    }
    info.completion_code=0;info.event_type=type;
    info.status=type==33 ? "xHCI command timed out; reboot required" :
                          "USB transfer timed out; reboot required";
    dma_unsafe=1; /* Do not recycle any DMA buffer after an uncompleted TD. */
    return -3; /* timeout: a submitted TRB may still be owned by the HBA */
}
static int command(unsigned type,uint32_t pointer,uint32_t flags,trb_t *event) {
    if (dma_unsafe) return -3;
    uint32_t cookie=enqueue(&commands,pointer,0,0,TRB_TYPE(type)|flags);
    wr(doorbell,0);
    int result=wait_event(33,cookie,event);
    if (result==-3) dma_unsafe=1;
    return result;
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
    if (!ready || busy || dma_unsafe) return;
    current=keyboard_device;
    if ((rd(op+4)&4) || !(rd(op+0x400+(root_port-1)*16)&1)) {
        ready=0;
        return;
    }
    /* Bound interrupt-handler work; never wait, enumerate or print here. */
    for (unsigned n=0;n<16;n++) {
        trb_t e;
        if (!next_event(&e)) break;
        keyboard_event(&e);
    }
}

static void keyboard_event(const trb_t *e) {
    if (!ready || !keyboard_device || ((e->flags>>10)&63)!=32 ||
        (e->flags>>24)!=keyboard_device->slot_id ||
        ((e->flags>>16)&31)!=keyboard_device->in_dci) return;
    unsigned code=e->size>>24;
    if (code!=1 && code!=13) {ready=0;return;}
    if (!(e->size&0xFFFFFF) && usb_process_boot_report(report,previous))
        kmemcpy(previous,report,8);
    device_t *saved=current;
    current=keyboard_device;
    arm_report();
    current=saved;
}

/* Only SCSI transparent, bulk-only, alternate-setting zero. Validate both
 * endpoints within the same interface; never reuse HID or hub endpoints. */
static int storage_descriptor(unsigned length, unsigned *config,
                              unsigned *in, unsigned *out,
                              unsigned *in_mps, unsigned *out_mps) {
    int selected=0;
    *in=*out=0;
    *config=data[5];
    for (unsigned p=0;p+2<=length;) {
        unsigned n=data[p], type=data[p+1];
        if (n<2 || p+n>length) return -1;
        if (type==4) {
            if (*in && *out) return 0;
            *in=*out=0;
            selected=n>=9 && data[p+3]==0 && data[p+5]==8 &&
                     data[p+6]==6 && data[p+7]==0x50;
        } else if (type==5 && selected && n>=7 && (data[p+3]&3)==2) {
            unsigned addr=data[p+2], mps=data[p+4]|(data[p+5]<<8);
            if (!(addr&15) || (addr&0x70) || !mps || mps>1024) return -1;
            if (addr&0x80) {*in=(addr&15)*2+1;*in_mps=mps;}
            else {*out=(addr&15)*2;*out_mps=mps;}
        }
        p+=n;
    }
    return *in && *out ? 0 : -1;
}
static int configure_storage(unsigned length) {
    unsigned cfg,in,out,in_mps=0,out_mps=0, disk;
    if (storage_descriptor(length,&cfg,&in,&out,&in_mps,&out_mps)) return -1;
    for (disk=0;disk<4 && disks[disk];disk++) {}
    if (disk==4 || control(0,9,cfg,0,0)) return -1;
    init_ring(&current->in);init_ring(&current->out);
    kmemset(input_context,0,sizeof(input_context));
    in_ctx(0)[1]=1u|(1u<<in)|(1u<<out);
    slot_context(in>out ? in : out);
    for (unsigned i=0;i<2;i++) {
        unsigned endpoint=i ? in : out, mps=i ? in_mps : out_mps;
        uint32_t *ep=in_ctx(endpoint+1);
        ep[1]=(3<<1)|((i ? 6u : 2u)<<3)|(mps<<16);
        ep[2]=phys(i ? current->in.trbs : current->out.trbs)|1;
        ep[4]=512;
    }
    trb_t event;
    if (command(12,phys(input_context),slot<<24,&event)) return -1;
    current->in_dci=in;current->out_dci=out;current->storage=1;
    disks[disk]=current;
    return 1;
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
    if (!slot || slot>max_devices) return -1;
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
    if (usb_parse_boot_keyboard(data,length,&info))
        return configure_storage(length);
    if (keyboard_device) return -1;
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
    keyboard_device=current;ready=1;arm_report();
    return 1;
}

static int release_device(device_t *dev) {
    current=dev;
    if (dma_unsafe) {dev->failed=1;return -1;}
    trb_t event;
    if (slot && command(10,0,slot<<24,&event)) {
        /* Keep every DMA buffer reserved if the controller cannot release it. */
        dev->failed=1;dma_unsafe=1;return -1;
    }
    if (slot) dcbaa[slot]=0;
    for (unsigned i=0;i<4;i++) if (disks[i]==dev) disks[i]=0;
    kmemset(dev,0,sizeof(*dev));
    return 0;
}
static void scan_ports(void) {
    info.connected_ports=0;info.failed_ports=0;
    for (unsigned port=1;port<=ports;port++)
        if (rd(op+0x400+(port-1)*16)&1) info.connected_ports++;
    for (unsigned i=0;i<max_devices;i++) {
        device_t *dev=&devices[i];
        if (dev->used && dev!=keyboard_device &&
            (dev->failed || !(rd(op+0x400+(dev->port-1)*16)&1)))
            release_device(dev);
        if (dma_unsafe) return;
    }
    for (unsigned port=1;port<=ports;port++) {
        device_t *available=0;int owned=0;
        for (unsigned i=0;i<max_devices;i++) {
            if (devices[i].used && devices[i].port==port) owned=1;
            if (!devices[i].used && !available) available=&devices[i];
        }
        if (owned || !available || !(rd(op+0x400+(port-1)*16)&1)) continue;
        current=available;current->used=1;current->port=port;
        if (keyboard_on_port(port)!=1) {
            info.failed_ports++;
            release_device(current);
        }
        if (dma_unsafe) return;
    }
}

/* Intel 7-series/Panther Point (including HM76). These registers are NOT
 * generic xHCI registers: never write them on other vendor/device IDs.
 * Firmware can leave even the USB2 wires of a USB3 socket under EHCI.
 * Enable only ports advertised as switchable; preserve other register bits.
 * Called once, after xHCI is running, never on storage rescans/eject.
 */
static void intel_route_ports(const pci_device_t *device) {
    if (device->vendor_id!=0x8086 || device->device_id!=0x1e31) return;
    const uint8_t masks[2]={0xdc,0xd4}, routes[2]={0xd8,0xd0};
    int changed=0;
    for (unsigned i=0;i<2;i++) {
        uint32_t mask=pci_read32(device->bus,device->device,device->function,masks[i]);
        uint32_t old=pci_read32(device->bus,device->device,device->function,routes[i]);
        if (!mask || mask==0xffffffffu || old==0xffffffffu) continue;
        pci_write32(device->bus,device->device,device->function,routes[i],old|mask);
        uint32_t actual=pci_read32(device->bus,device->device,device->function,routes[i]);
        if ((actual&mask)!=mask) {
            info.status="Intel USB port routing was rejected by firmware";
            continue;
        }
        info.intel_routing=1;
        if ((old&mask)!=mask) changed=1;
    }
    if (changed) timer_sleep(100); /* allow switched ports to reconnect */
}

static int controller(const pci_device_t *device) {
    info.found=1;info.vendor_id=device->vendor_id;info.device_id=device->device_id;
    info.bus=device->bus;info.device=device->device;info.function=device->function;
    info.intel_routing=0;
    info.status="Unsupported or unassigned xHCI MMIO address";
    if ((device->bar[0]&1) || !(device->bar[0]&0xFFFFFFF0u) ||
        ((device->bar[0]&6)==4 && device->bar[1])) return 0;
    uint32_t base=device->bar[0]&0xFFFFFFF0u;
    pci_enable_busmaster(device);
    uint32_t cap=rd(base), hcs=rd(base+4), hcc=rd(base+0x10);
    info.status="xHCI registers unavailable (firmware or power configuration)";
    if (!cap || cap==0xffffffffu || hcs==0xffffffffu || hcc==0xffffffffu) return 0;
    ports=hcs>>24;
    info.status="Invalid xHCI capability registers";
    if ((cap&255)<0x20 || (cap&3) || !(hcs&255) || !ports) return 0;
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
            info.status="BIOS did not release xHCI ownership";
            if (wait_bits(ext,1u<<16,0,5000)) return 0;
            wr(ext+4,0);
            break;
        }
        unsigned next=(v>>8)&255;
        if (!next) break;
        ext+=next*4;
    }
    /* Real controllers need CNR clear BEFORE operational register writes,
     * not only after HCRST. Do not force a still-firmware-owned controller.
     */
    info.status="xHCI controller not ready (CNR timeout)";
    if (wait_bits(op+4,1u<<11,0,5000)) return 0;
    wr(op,rd(op)&~(1u|4u|8u)); /* halt, disable interrupt delivery */
    info.status="xHCI controller did not halt";
    if (wait_bits(op+4,1,1,1000)) return 0;
    wr(op,2);
    info.status="xHCI controller reset/readiness timed out";
    if (wait_bits(op,2,0,1000) || wait_bits(op+4,1u<<11,0,1000)) return 0;
    info.status="xHCI controller does not support 4 KiB pages";
    if (!(rd(op+8)&1)) return 0;
    unsigned pads=((rd(base+8)>>27)&31)|(((rd(base+8)>>21)&31)<<5);
    info.status="xHCI requires too many scratchpad pages";
    if (pads>32) return 0;
    init_ring(&commands);
    kmemset((void *)events,0,sizeof(events));
    kmemset(dcbaa,0,sizeof(dcbaa));
    event_index=0;event_cycle=1;
    max_devices=(hcs&255)<5 ? (hcs&255) : 5;
    kmemset(devices,0,sizeof(devices));
    for (unsigned i=0;i<pads;i++) scratch_list[i]=phys(scratch_pages[i]);
    if (pads) dcbaa[0]=phys(scratch_list);
    wr64(op+0x30,phys(dcbaa));
    wr64(op+0x18,phys(commands.trbs)|1);
    wr(op+0x38,max_devices);
    erst[0]=phys(events);erst[1]=0;erst[2]=RING_SIZE;erst[3]=0;
    wr(interrupter+8,1);wr64(interrupter+0x10,phys(erst));
    wr64(interrupter+0x18,phys(events));wr(interrupter,1);
    wr(op,1); /* Run; interrupt delivery disabled */
    info.status="xHCI controller did not start; reboot required";
    if (wait_bits(op+4,1,0,1000)) {dma_unsafe=1;return 0;}
    running=1;
    info.status="xHCI controller running";
    intel_route_ports(device);
    scan_ports();
    return 1;
}
int usb_xhci_init(void) {
#if !CONFIG_ENABLE_PCI
    info.status="PCI probing disabled in this build; use a modern profile";
    return 0; /* Respect profiles that explicitly disable PCI probing. */
#endif
    if (initialized) return ready;
    initialized=1;busy=1;
    ready=0;
    if (dma_unsafe) {busy=0;return 0;}
    pci_device_t current, next;
    info.status="No xHCI controller found";
    if (pci_find_class(0x0C,0x03,&current)) {busy=0;return 0;}
    do {
        if (current.prog_if==0x30 && controller(&current)) break;
        if (dma_unsafe) break;
        if (pci_find_class_after(0x0C,0x03,&current,&next)) break;
        current=next;
    } while (1);
    busy=0;return ready;
}

int usb_xhci_storage_scan(void) {
    usb_xhci_init();
    if (!running || dma_unsafe) return -1;
    busy=1;scan_ports();busy=0;
    int count=0;
    for (unsigned i=0;i<4;i++) if (usb_xhci_storage_present(i)) count++;
    return count;
}
int usb_xhci_storage_present(unsigned disk) {
    return disk<4 && disks[disk] && !disks[disk]->failed &&
           (rd(op+0x400+(disks[disk]->port-1)*16)&1);
}
static int bulk(ring_t *ring,unsigned endpoint,unsigned bytes,int write) {
    if (!bytes || bytes>sizeof(bulk_data)) return -1;
    uint32_t cookie=enqueue(ring,phys(bulk_data),0,bytes,TRB_TYPE(1)|(1<<5));
    wr(doorbell+slot*4,endpoint);
    trb_t event;
    if (wait_event(32,cookie,&event) || (event.size&0xFFFFFF)) return -1;
    __asm__ volatile("":::"memory");
    (void)write;
    return 0;
}
static int bot_bulk(void *device,uint8_t *buffer,unsigned bytes,int write) {
    (void)device;(void)buffer; /* shared BOT scratch is bulk_data */
    return bulk(write ? &current->out : &current->in,
                write ? current->out_dci : current->in_dci,bytes,write);
}
int usb_xhci_storage_command(unsigned disk,const uint8_t *cdb,unsigned cdb_len,
                             uint8_t *buffer,unsigned bytes,int write) {
    if (!usb_xhci_storage_present(disk) || !cdb || !cdb_len ||
        cdb_len>16 || bytes>512 || (bytes && !buffer) || busy || dma_unsafe) return -1;
    busy=1;current=disks[disk];
    int result=usb_bot_command(bot_bulk,current,++storage_tag,bulk_data,data,
                               cdb,cdb_len,buffer,bytes,write);
    if (result==-1) goto fail;
    busy=0;return result;
fail:
    /* A stalled/timed-out transport may still own rings/buffers. Do not
     * recycle it or claim safe removal; release requires Disable Slot. */
    current->failed=1;
    trb_t event;
    if (command(10,0,slot<<24,&event)) dma_unsafe=1;
    else {dcbaa[slot]=0;slot=0;}
    busy=0;return -1;
}
int usb_xhci_storage_release(unsigned disk) {
    if (disk>=4 || !disks[disk] || busy) return -1;
    busy=1;int result=release_device(disks[disk]);busy=0;
    return result;
}
