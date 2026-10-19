/* Exercise the actual xHCI initialization with deterministic PCI/MMIO.
 * No host ports, physical USB devices, or disk contents are accessed.
 */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#define CONFIG_ENABLE_PCI 1
#define USB_XHCI_TEST
#include "../kernel/usb_xhci.c"

#define BASE 0x10000u
#define OP (BASE+0x40)
#define LEGACY (BASE+0x80)
static uint32_t regs[0x2000/4], cfg[64], ticks, ready_at, bios_at;
static unsigned route_writes, operational_writes, refusals, pci_reads;
static unsigned legacy_writes, smi_writes;
static int refuse_bios, stuck_cnr, refuse_halt, refuse_reset, refuse_start;
static int reject_routes, reject_takeover, reclaim_ownership, refuse_smi, reject_os;
static uint32_t rd(uint32_t address) {
    assert(address>=BASE && address<BASE+sizeof(regs) && !(address&3));
    return regs[(address-BASE)/4];
}
static void wr(uint32_t address,uint32_t value) {
    assert(address>=BASE && address<BASE+sizeof(regs) && !(address&3));
    if (address==LEGACY) {
        legacy_writes++;
        if (!(value&(1u<<16)) && (rd(LEGACY)&(1u<<16))) {
            assert(!(rd(LEGACY+4)&LEGACY_SMI_ENABLES));
            if (reject_takeover) value|=1u<<16;
        }
        if (reject_os) value&=~(1u<<24);
        regs[(address-BASE)/4]=value;
        bios_at=ticks+2;
        return;
    }
    if (address==LEGACY+4) {
        smi_writes++;
        if (refuse_smi) return;
        uint32_t old=rd(address);
        regs[(address-BASE)/4]=(value&~LEGACY_SMI_EVENTS)|
                               (old&LEGACY_SMI_EVENTS&~value);
        return;
    }
    if (address==OP) {
        operational_writes++;
        assert(!(rd(LEGACY)&(1u<<16))); /* verify ownership before reset */
        assert(rd(LEGACY)&(1u<<24));
        assert(!(rd(LEGACY+4)&LEGACY_SMI_ENABLES));
        assert(!(rd(OP+4)&(1u<<11)));   /* CNR must clear before writes */
        if (value&2) {
            regs[(OP-BASE)/4]=refuse_reset ? 2 : 0;
            regs[(OP+4-BASE)/4]=1|(1u<<11);
            ready_at=ticks+2;
        } else {
            regs[(OP-BASE)/4]=value;
            if (!(value&1)) {
                if (!refuse_halt) regs[(OP+4-BASE)/4]|=1;
            } else if (!refuse_start) regs[(OP+4-BASE)/4]&=~1u;
        }
        return;
    }
    regs[(address-BASE)/4]=value;
}
static void idle(void) {
    ticks++;
    if (!refuse_bios && ticks>=bios_at) regs[(LEGACY-BASE)/4]&=~(1u<<16);
    if (!stuck_cnr && ticks>=ready_at) regs[(OP+4-BASE)/4]&=~(1u<<11);
}
uint32_t timer_get_ticks(void) {return ticks;}
void timer_sleep(uint32_t ms) {
    ticks+=ms;
    if (reclaim_ownership && !(rd(LEGACY)&(1u<<16)))
        regs[(LEGACY-BASE)/4]|=1u<<16;
}
void *kmemset(void *p,int c,size_t n) {return memset(p,c,n);}
void *kmemcpy(void *p,const void *q,size_t n) {return memcpy(p,q,n);}
int usb_parse_boot_keyboard(const uint8_t *p,unsigned n,usb_keyboard_descriptor_t *out) {
    (void)p;(void)n;(void)out;return -1;
}
int usb_process_boot_report(const uint8_t a[8],const uint8_t b[8]) {
    (void)a;(void)b;return 0;
}
uint32_t pci_read32(uint8_t bus,uint8_t dev,uint8_t fn,uint8_t reg) {
    (void)bus;(void)dev;(void)fn;pci_reads++;return cfg[reg/4];
}
void pci_write32(uint8_t bus,uint8_t dev,uint8_t fn,uint8_t reg,uint32_t value) {
    (void)bus;(void)dev;(void)fn;
    assert(reg==0xd8 || reg==0xd0);
    /* USB3 terminations are enabled before routing USB2 data wires. */
    assert(reg==(route_writes ? 0xd0 : 0xd8));
    route_writes++;
    if (!reject_routes) cfg[reg/4]=value;
    else refusals++;
}
void pci_enable_busmaster(const pci_device_t *p) {(void)p;}
int pci_find_class(uint8_t cls,uint8_t sub,pci_device_t *p) {
    (void)cls;(void)sub;(void)p;return -1;
}
int pci_find_class_after(uint8_t cls,uint8_t sub,const pci_device_t *prev,
                         pci_device_t *p) {
    (void)cls;(void)sub;(void)prev;(void)p;return -1;
}

static pci_device_t fresh(void) {
    memset(regs,0,sizeof(regs));memset(cfg,0,sizeof(cfg));
    memset(&info,0,sizeof(info));
    memset(disks,0,sizeof(disks));memset(devices,0,sizeof(devices));
    keyboard_device=0;ready=running=dma_unsafe=initialized=0;
    ticks=0;ready_at=5;bios_at=2;
    refuse_bios=stuck_cnr=refuse_halt=refuse_reset=refuse_start=reject_routes=0;
    reject_takeover=reclaim_ownership=refuse_smi=reject_os=0;
    legacy_writes=smi_writes=0;
    route_writes=operational_writes=refusals=pci_reads=0;
    regs[0]=0x01000040; /* HCIVERSION 1.0, CAPLENGTH 0x40 */
    regs[1]=(1u<<24)|5; /* one port, five device slots */
    regs[4]=32u<<16; /* legacy capability at +0x80 */
    regs[5]=0x1000;regs[6]=0x800;
    regs[(LEGACY-BASE)/4]=1|(1u<<16);
    regs[(LEGACY+4-BASE)/4]=LEGACY_SMI_ENABLES|LEGACY_SMI_EVENTS|2;
    regs[(OP+4-BASE)/4]=1|(1u<<11);
    regs[(OP+8-BASE)/4]=1;
    cfg[0xdc/4]=3;cfg[0xd8/4]=0x80;
    cfg[0xd4/4]=0x15;cfg[0xd0/4]=0x40;
    pci_device_t p={.vendor_id=0x8086,.device_id=0x1e31,.bus=0,
                    .device=20,.function=0,.bar={BASE}};
    return p;
}
int main(void) {
    pci_device_t p=fresh();
    assert(!usb_xhci_init() && !info.found && !route_writes);
    assert(strstr(info.status,"No xHCI controller found"));
    p=fresh();
    assert(controller(&p)==1 && running && !dma_unsafe);
    assert(!info.bios_handoff_fallback && legacy_writes==1 && smi_writes==1);
    assert(rd(LEGACY+4)==2); /* reserved bit kept, W1C events acknowledged */
    assert(route_writes==2 && cfg[0xd8/4]==0x83 && cfg[0xd0/4]==0x55);
    assert(info.intel_routing && info.device_id==0x1e31 && !info.connected_ports);
    unsigned writes=operational_writes;
    scan_ports();scan_ports();
    assert(route_writes==2 && operational_writes==writes); /* no reset/reroute */
    assert(legacy_writes==1 && smi_writes==1); /* no repeated takeover */

    p=fresh();p.vendor_id=0x1234;
    assert(controller(&p)==1 && !route_writes && !pci_reads);
    p=fresh();p.device_id=0x8c31;
    assert(controller(&p)==1 && !route_writes && !pci_reads);
    p=fresh();cfg[0xdc/4]=cfg[0xd4/4]=0xffffffffu;
    assert(controller(&p)==1 && !route_writes && !info.intel_routing);
    p=fresh();cfg[0xdc/4]=cfg[0xd4/4]=0;
    assert(controller(&p)==1 && !route_writes && !info.intel_routing);
    p=fresh();reject_routes=1;
    assert(controller(&p)==1 && refusals==2 && !info.intel_routing);
    assert(strstr(info.status,"routing was rejected"));

    p=fresh();refuse_bios=1;
    assert(controller(&p)==1 && running && info.bios_handoff_fallback);
    assert(legacy_writes==2 && smi_writes==1 && route_writes==2 && ticks>=5000);
    assert((info.legacy_support&((1u<<16)|(1u<<24)))==(1u<<24));
    assert(!(info.legacy_control&LEGACY_SMI_ENABLES));
    scan_ports();
    assert(legacy_writes==2 && smi_writes==1);

    p=fresh();refuse_bios=1;p.vendor_id=0x1234;
    assert(!controller(&p) && !running && !operational_writes && !route_writes);
    assert(strstr(info.status,"BIOS did not release"));
    assert(legacy_writes==1 && !smi_writes && !info.bios_handoff_fallback);
    p=fresh();refuse_bios=1;p.device_id=0x8c31;
    assert(!controller(&p) && !operational_writes && !route_writes);
    assert(legacy_writes==1 && !smi_writes);
    p=fresh();refuse_bios=1;reject_takeover=1;
    assert(!controller(&p) && !operational_writes && !route_writes);
    assert(strstr(info.status,"fallback rejected") && !info.bios_handoff_fallback);
    assert(info.legacy_support&(1u<<16));
    p=fresh();refuse_bios=1;reclaim_ownership=1;
    assert(!controller(&p) && !operational_writes && !route_writes);
    assert(strstr(info.status,"Firmware reclaimed"));
    p=fresh();refuse_bios=1;refuse_smi=1;
    assert(!controller(&p) && !operational_writes && !route_writes);
    assert(strstr(info.status,"Cannot disable") && legacy_writes==1);
    p=fresh();refuse_smi=1;
    assert(!controller(&p) && !operational_writes && !route_writes);
    assert(strstr(info.status,"Cannot disable"));
    p=fresh();reject_os=1;
    assert(!controller(&p) && !operational_writes && !route_writes);
    assert(strstr(info.status,"OS ownership request was rejected"));
    p=fresh();regs[(LEGACY+4-BASE)/4]=0xffffffffu;
    assert(!controller(&p) && !operational_writes && !route_writes);
    assert(strstr(info.status,"legacy control registers unavailable"));
    p=fresh();stuck_cnr=1;
    assert(!controller(&p) && !operational_writes && !route_writes);
    assert(strstr(info.status,"CNR timeout"));
    p=fresh();refuse_halt=1;regs[(OP+4-BASE)/4]&=~1u;
    assert(!controller(&p) && !route_writes);
    assert(strstr(info.status,"did not halt"));
    p=fresh();refuse_reset=1;
    assert(!controller(&p) && !route_writes);
    assert(strstr(info.status,"reset/readiness"));
    p=fresh();refuse_start=1;
    assert(!controller(&p) && dma_unsafe && !route_writes);
    assert(strstr(info.status,"did not start"));
    p=fresh();p.bar[0]=0;
    assert(!controller(&p) && !operational_writes && !route_writes);
    assert(strstr(info.status,"MMIO address"));
    p=fresh();regs[0]=0xffffffffu;
    assert(!controller(&p) && !operational_writes && !route_writes);
    assert(strstr(info.status,"registers unavailable"));
    p=fresh();regs[(OP+8-BASE)/4]=0;
    assert(!controller(&p) && !route_writes);
    assert(strstr(info.status,"4 KiB pages"));

    /* A missing completion must preserve all potentially DMA-owned state. */
    p=fresh();assert(controller(&p));
    trb_t event;
    assert(wait_event(32,0x1234,&event)==-3 && dma_unsafe);
    assert(info.event_type==32 && !info.completion_code);
    devices[0].used=1;devices[0].slot_id=1;
    assert(release_device(&devices[0])==-1 && devices[0].used);
    assert(command(10,0,0,&event)==-3);
    puts("PASS: Intel routing masks/isolation, initial CNR, verified Intel "
         "ownership fallback, SMI masks/W1C, takeover failures, firmware reclaim, "
         "bounded startup failures, no rescan reset, DMA timeout quarantine");
    return 0;
}
