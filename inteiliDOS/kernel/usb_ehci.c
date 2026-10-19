/* EHCI high-speed root-port BOT. No periodic schedule, hub/TT, or IRQ owner.
 * One controller; 32-bit identity DMA. Every schedule is retired before its
 * descriptors are reused. Failure to retire pins all DMA until reboot.
 * Companion-owned ports (including the native keyboard) are never touched.
 */
#include "usb_ehci.h"
#include "usb_bot.h"
#include "pci.h"
#include "timer.h"
#include "memory.h"

typedef struct {
    uint32_t next, alt, token, page[5], high[5];
} __attribute__((aligned(32))) qtd_t;
typedef struct {
    uint32_t link, endpoint, caps, current;
    uint32_t next, alt, token, page[5], high[5];
} __attribute__((aligned(32))) qh_t;
typedef struct {
    unsigned port, address, in, out, in_toggle, out_toggle, failed;
} device_t;
static device_t disks[4];
static unsigned ignored[16], ports, next_address;
static qh_t head;
static qtd_t td[3];
static uint8_t setup[8] __attribute__((aligned(4096)));
static uint8_t data[512] __attribute__((aligned(4096)));
static uint8_t scratch[512] __attribute__((aligned(4096))), saved[512];
static uint32_t op, tag;
static int initialized, running, unsafe, busy;
static usb_ehci_info_t info={.status="EHCI has not been initialized"};
const usb_ehci_info_t *usb_ehci_info(void) {return &info;}
static uint32_t phys(const void *p) {return (uint32_t)(uintptr_t)p;}
#ifdef USB_EHCI_TEST
#include "../tests/usb_ehci_test_io.h"
#else
static uint32_t rd(uint32_t p) {return *(volatile uint32_t *)(uintptr_t)p;}
static void wr(uint32_t p,uint32_t v) {
    __asm__ volatile("":::"memory");
    *(volatile uint32_t *)(uintptr_t)p=v;
}
static void idle(void) {__asm__ volatile("hlt");}
#endif
static int wait_bits(uint32_t p,uint32_t mask,uint32_t value,unsigned ms) {
    uint32_t start=timer_get_ticks();
    while ((rd(p)&mask)!=value) {
        if (timer_get_ticks()-start>=ms) return -1;
        idle();
    }
    return 0;
}
static uint32_t port_reg(unsigned port) {return op+0x44+(port-1)*4;}
static void port_write(unsigned port,uint32_t clear,uint32_t set) {
    uint32_t p=port_reg(port);
    wr(p,(rd(p)&~(0x2Au|clear))|set); /* never acknowledge W1C changes by accident */
}
static int stop_schedule(void) {
    wr(op,rd(op)&~(1u<<5));
    if (wait_bits(op+4,1u<<15,0,1000)) {
        unsafe=1;info.status="EHCI async retirement timed out; reboot required";return -1;
    }
    __asm__ volatile("":::"memory");
    return 0;
}
static void descriptor(qtd_t *t,unsigned pid,unsigned bytes,unsigned toggle,void *buffer) {
    kmemset(t,0,sizeof(*t));t->next=t->alt=1;
    t->token=(toggle<<31)|(bytes<<16)|(3u<<10)|(pid<<8)|0x80;
    t->page[0]=phys(buffer);
}
/* DTC=1: qTDs carry control/bulk toggles. One QH, circular async head. */
static int transfer(unsigned address,unsigned endpoint,unsigned count) {
    if (unsafe || stop_schedule()) return -1;
    kmemset(&head,0,sizeof(head));
    head.link=phys(&head)|2;
    head.endpoint=address|(endpoint<<8)|(2u<<12)|(1u<<14)|(1u<<15)|(64u<<16);
    if (endpoint) head.endpoint=(head.endpoint&~(0x7FFu<<16))|(512u<<16);
    head.next=phys(td);head.alt=1;
    wr(op+0x18,phys(&head));
    __asm__ volatile("":::"memory");
    wr(op,rd(op)|(1u<<5));
    uint32_t start=timer_get_ticks();
    int result=-1;
    for (;;) {
        __asm__ volatile("":::"memory");
        unsigned active=0, error=0;
        for (unsigned i=0;i<count;i++) {
            uint32_t token=((volatile qtd_t *)td)[i].token;
            active|=token&0x80;error|=token&0x7C;
        }
        if (error) break;
        /* A short control IN can skip the data TD via alternate-next. */
        if (!(((volatile qtd_t *)td)[count-1].token&0x80)) {
            result=0;break;
        }
        if (!active || timer_get_ticks()-start>=1000) break;
        idle(); /* timer IRQ continues polling UHCI/xHCI keyboard */
    }
    if (stop_schedule()) return -1;
    if (result) info.status="EHCI USB transfer failed or timed out";
    return result;
}
static int control(unsigned address,uint8_t type,uint8_t request,
                   uint16_t value,uint16_t index,unsigned length) {
    setup[0]=type;setup[1]=request;setup[2]=value;setup[3]=value>>8;
    setup[4]=index;setup[5]=index>>8;setup[6]=length;setup[7]=length>>8;
    descriptor(td,2,8,0,setup);
    unsigned status=length ? 2 : 1;
    td[0].next=phys(&td[1]);
    if (length) {
        descriptor(&td[1],type&0x80 ? 1 : 0,length,1,data);
        td[1].next=td[1].alt=phys(&td[2]);
    }
    descriptor(&td[status],!length || !(type&0x80) ? 1 : 0,0,1,data);
    return transfer(address,0,status+1);
}
static int get_descriptor(unsigned address,unsigned type,unsigned length) {
    kmemset(data,0,sizeof(data));
    if (control(address,0x80,6,type<<8,0,length)) return -1;
    return (td[1].token>>16)&0x7FFF ? -1 : 0;
}
static int enumerate(device_t *d,unsigned port) {
    port_write(port,4,1u<<8);timer_sleep(50);
    port_write(port,1u<<8,0);
    if (wait_bits(port_reg(port),1u<<8,0,250)) return -1;
    timer_sleep(20);
    if (!(rd(port_reg(port))&4)) {
        /* Full/low-speed belongs to UHCI/OHCI, never use split transactions. */
        port_write(port,0,1u<<13);return 0;
    }
    if (next_address>127 || get_descriptor(0,1,18) || data[0]<18 ||
        data[1]!=1 || data[7]!=64) return -1;
    unsigned address=next_address++;
    if (control(0,0,5,address,0,0)) return -1;
    timer_sleep(2);
    if (get_descriptor(address,2,9) || data[0]!=9 || data[1]!=2) return -1;
    unsigned length=data[2]|(data[3]<<8);
    if (length<9 || length>512 || get_descriptor(address,2,length)) return -1;
    unsigned cfg=data[5], in=0,out=0,selected=0;
    for (unsigned p=0;p+2<=length;) {
        unsigned n=data[p],type=data[p+1];
        if (n<2 || p+n>length) return -1;
        if (type==4) {
            if (in && out) break;
            in=out=0;
            selected=n>=9 && !data[p+3] && data[p+5]==8 &&
                     data[p+6]==6 && data[p+7]==0x50;
        } else if (type==5 && selected && n>=7 && (data[p+3]&3)==2) {
            unsigned ep=data[p+2], mps=data[p+4]|(data[p+5]<<8);
            if (!(ep&15) || (ep&0x70) || mps!=512) return -1;
            if (ep&0x80) in=ep&15;else out=ep&15;
        }
        p+=n;
    }
    if (!in || !out) return 0; /* leave non-storage unconfigured */
    if (!cfg || control(address,0,9,cfg,0,0)) return -1;
    port_write(port,0,2); /* baseline: notice unplug/replacement, not old CSC */
    *d=(device_t){.port=port,.address=address,.in=in,.out=out};
    return 1;
}
static int controller(const pci_device_t *dev) {
    info.found=1;info.bus=dev->bus;info.device=dev->device;info.function=dev->function;
    info.vendor_id=dev->vendor_id;info.device_id=dev->device_id;
    info.status="Unsupported or unassigned EHCI MMIO address";
    if ((dev->bar[0]&1) || !(dev->bar[0]&~15u) ||
        ((dev->bar[0]&6)==4 && dev->bar[1])) return 0;
    uint32_t base=dev->bar[0]&~15u;
    uint32_t cap=rd(base),hcs=rd(base+4),hcc=rd(base+8);
    info.status="Invalid EHCI capability registers";
    if (cap==0xFFFFFFFF || (cap&255)<0x10 || (cap&3) ||
        (cap>>16)!=0x100 || hcs==0xFFFFFFFF || hcc==0xFFFFFFFF || !(hcs&15)) return 0;
    /* PCI extended capability chain: request ownership, never force BIOS. */
    unsigned ext=(hcc>>8)&255;
    for (unsigned n=0;ext;n++) {
        if (n==48 || ext<0x40 || ext>0xF8 || (ext&3)) {
            info.status="Invalid EHCI firmware capability chain";return 0;
        }
        uint32_t v=pci_read32(dev->bus,dev->device,dev->function,ext);
        if ((v&255)==1) {
            pci_write32(dev->bus,dev->device,dev->function,ext,v|(1u<<24));
            uint32_t start=timer_get_ticks();
            while (pci_read32(dev->bus,dev->device,dev->function,ext)&(1u<<16)) {
                if (timer_get_ticks()-start>=5000) {
                    info.status="BIOS did not release EHCI ownership";return 0;
                }
                idle();
            }
            pci_write32(dev->bus,dev->device,dev->function,ext+4,0);
            break;
        }
        ext=(v>>8)&255;
    }
    pci_enable_busmaster(dev);
    op=base+(cap&255);ports=hcs&15;
    wr(op+8,0); /* polled: no interrupt delivery */
    wr(op,rd(op)&~(1u|(1u<<4)|(1u<<5)));
    info.status="EHCI controller did not halt";
    if (wait_bits(op+4,1u<<12,1u<<12,1000)) return 0;
    wr(op,2);
    info.status="EHCI controller reset timed out";
    if (wait_bits(op,2,0,1000)) {unsafe=1;return 0;}
    wr(op+0x10,0); /* CTRLDSSEGMENT: all DMA below 4 GiB */
    wr(op+8,0);wr(op,1);
    info.status="EHCI controller did not start";
    if (wait_bits(op+4,1u<<12,0,1000)) {unsafe=1;return 0;}
    wr(op+0x40,1); /* route once, BEFORE companion keyboard initialization */
    for (unsigned p=1;p<=ports;p++) if (hcs&(1u<<4)) port_write(p,0,1u<<12);
    timer_sleep(100);
    /* Return ALL existing full/low-speed ports before UHCI starts, even when
     * no storage slots remain. High-speed mass storage is enumerated on mount. */
    for (unsigned p=1;p<=ports;p++) {
        if (!(rd(port_reg(p))&1) || (rd(port_reg(p))&(1u<<13))) continue;
        port_write(p,4,1u<<8);timer_sleep(50);port_write(p,1u<<8,0);
        if (wait_bits(port_reg(p),1u<<8,0,250)) {ignored[p]=1;continue;}
        timer_sleep(20);
        if (!(rd(port_reg(p))&4)) port_write(p,0,1u<<13);
    }
    running=1;next_address=1;info.status="EHCI controller running";
    return 1;
}
int usb_ehci_init(void) {
#if !CONFIG_ENABLE_PCI
    info.status="PCI probing disabled in this build";return 0;
#endif
    if (initialized) return running;
    initialized=1;
    pci_device_t dev,next;
    info.status="No EHCI controller found";
    if (pci_find_class(0x0C,3,&dev)) return 0;
    do {
        if (dev.prog_if==0x20 && controller(&dev)) return 1;
        if (unsafe || pci_find_class_after(0x0C,3,&dev,&next)) break;
        dev=next;
    } while (1);
    return 0;
}
int usb_ehci_storage_present(unsigned disk) {
    return disk<4 && disks[disk].port && !disks[disk].failed &&
           (rd(port_reg(disks[disk].port))&7)==5;
}
void usb_ehci_storage_forget_absent(void) {
    if (!running || unsafe || busy) return;
    for (unsigned i=0;i<4;i++) {
        if (!disks[i].port) continue;
        uint32_t v=rd(port_reg(disks[i].port));
        if (!(v&1) || (v&2)) {
            /* Only explicit mount, AFTER old filesystem bindings are gone.
             * Eject must not replace a missing disk with a new arrival. */
            kmemset(&disks[i],0,sizeof(disks[i]));
        }
    }
}
int usb_ehci_storage_scan(void) {
    if (!usb_ehci_init() || unsafe || busy) return -1;
    busy=1;info.connected_ports=info.failed_ports=0;
    for (unsigned p=1;p<=ports;p++) {
        uint32_t v=rd(port_reg(p));
        if (!(v&1)) {ignored[p]=0;continue;}
        info.connected_ports++;
        if ((v&(1u<<13)) || ignored[p]) continue;
        unsigned i;
        for (i=0;i<4;i++) if (disks[i].port==p) break;
        if (i<4) continue; /* includes quarantined devices */
        for (i=0;i<4 && disks[i].port;i++) {}
        if (i==4) continue;
        int rc=enumerate(&disks[i],p);
        if (rc<=0) {ignored[p]=1;if (rc<0) info.failed_ports++;}
        if (unsafe) break;
    }
    int count=0;
    for (unsigned i=0;i<4;i++) count+=usb_ehci_storage_present(i);
    busy=0;return unsafe ? -1 : count;
}
static int bulk(void *device,uint8_t *buffer,unsigned bytes,int write) {
    device_t *d=device;
    unsigned *toggle=write ? &d->out_toggle : &d->in_toggle;
    descriptor(td,write ? 0 : 1,bytes,*toggle,buffer);
    if (transfer(d->address,write ? d->out : d->in,1) ||
        ((td[0].token>>16)&0x7FFF)) return -1;
    *toggle^=((bytes+511)/512)&1;
    return 0;
}
int usb_ehci_storage_command(unsigned disk,const uint8_t *cdb,unsigned len,
                             uint8_t *buffer,unsigned bytes,int write) {
    if (!usb_ehci_storage_present(disk) || unsafe || busy ||
        !cdb || !len || len>16 || bytes>512 || (bytes && !buffer)) return -1;
    busy=1;
    int rc=usb_bot_command(bulk,&disks[disk],++tag,scratch,saved,cdb,len,buffer,bytes,write);
    if (rc==-1) disks[disk].failed=1;
    busy=0;return rc;
}
int usb_ehci_storage_release(unsigned disk) {
    if (disk>=4 || !disks[disk].port || disks[disk].failed || unsafe || busy) return -1;
    /* There are no live TDs after a command. Disable only this storage port. */
    unsigned port=disks[disk].port;
    port_write(port,4,0);
    if (wait_bits(port_reg(port),4,0,250)) return -1;
    kmemset(&disks[disk],0,sizeof(disks[disk]));return 0;
}
