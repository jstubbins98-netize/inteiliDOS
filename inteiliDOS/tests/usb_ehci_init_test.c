/* Actual EHCI startup/retirement code against deterministic PCI/MMIO.
 * No hardware or non-disposable media is accessed. */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#define CONFIG_ENABLE_PCI 1
#define USB_EHCI_TEST
#include "../kernel/usb_ehci.c"
#define BASE 0x10000u
#define OP (BASE+0x20)
static uint32_t regs[256],cfg[64],ticks;
static unsigned writes,busmaster,port_writes;
static int bios_refuses,halt_refuses,reset_refuses,start_refuses,retire_refuses;
static uint32_t rd(uint32_t p) {
    assert(p>=BASE && p<BASE+sizeof(regs) && !(p&3));
    return regs[(p-BASE)/4];
}
static void wr(uint32_t p,uint32_t v) {
    assert(!(cfg[0x40/4]&(1u<<16)));
    writes++;
    if (p==OP) {
        if (v&2) v=reset_refuses ? 2 : 0;
        if (v&1) {
            if (!start_refuses) regs[(OP+4-BASE)/4]&=~(1u<<12);
        } else if (!halt_refuses) regs[(OP+4-BASE)/4]|=1u<<12;
        if (v&(1u<<5)) regs[(OP+4-BASE)/4]|=1u<<15;
        else if (!retire_refuses) regs[(OP+4-BASE)/4]&=~(1u<<15);
    }
    if (p>=OP+0x44 && p<OP+0x44+8) {
        port_writes++;
        assert(!(rd(p)&(1u<<13))); /* companion keyboard is untouchable */
    }
    regs[(p-BASE)/4]=v;
}
static void idle(void) {ticks++;}
uint32_t timer_get_ticks(void) {return ticks;}
void timer_sleep(uint32_t ms) {ticks+=ms;}
void *kmemset(void *p,int c,size_t n) {return memset(p,c,n);}
void *kmemcpy(void *p,const void *q,size_t n) {return memcpy(p,q,n);}
void pci_enable_busmaster(const pci_device_t *d) {(void)d;busmaster++;}
uint32_t pci_read32(uint8_t b,uint8_t d,uint8_t f,uint8_t r) {
    (void)b;(void)d;(void)f;return cfg[r/4];
}
void pci_write32(uint8_t b,uint8_t d,uint8_t f,uint8_t r,uint32_t v) {
    (void)b;(void)d;(void)f;
    if (r==0x40 && !bios_refuses) v&=~(1u<<16);
    cfg[r/4]=v;
}
int pci_find_class(uint8_t a,uint8_t b,pci_device_t *d) {
    (void)a;(void)b;(void)d;return -1;
}
int pci_find_class_after(uint8_t a,uint8_t b,const pci_device_t *p,pci_device_t *d) {
    (void)a;(void)b;(void)p;(void)d;return -1;
}
static pci_device_t fresh(void) {
    memset(regs,0,sizeof(regs));memset(cfg,0,sizeof(cfg));
    memset(disks,0,sizeof(disks));memset(ignored,0,sizeof(ignored));
    initialized=running=unsafe=busy=0;ticks=writes=busmaster=port_writes=0;
    bios_refuses=halt_refuses=reset_refuses=start_refuses=retire_refuses=0;
    info=(usb_ehci_info_t){0};
    regs[0]=0x01000020;regs[1]=2;regs[2]=0x4000;
    regs[(OP+4-BASE)/4]=1u<<12;
    cfg[0x40/4]=1|(1u<<16);
    return (pci_device_t){.vendor_id=0x8086,.device_id=0x293a,.bar={BASE}};
}
int main(void) {
    pci_device_t p=fresh();
    assert(!usb_ehci_init() && !writes && !busmaster);
    p=fresh();bios_refuses=1;
    assert(!controller(&p) && !writes && !busmaster && ticks==5000);
    assert(strstr(info.status,"BIOS did not release"));
    p=fresh();p.bar[0]=0;assert(!controller(&p) && !writes);
    p=fresh();p.bar[0]=BASE|4;p.bar[1]=1;assert(!controller(&p) && !writes);
    p=fresh();regs[0]=0xFFFFFFFF;assert(!controller(&p) && !writes);
    p=fresh();regs[2]=0x2000;assert(!controller(&p) && !writes);
    p=fresh();cfg[0x40/4]=0x4002; /* cyclic unknown capability */
    assert(!controller(&p) && !writes);
    p=fresh();halt_refuses=1;regs[(OP+4-BASE)/4]=0;
    assert(!controller(&p) && !running);
    p=fresh();reset_refuses=1;
    assert(!controller(&p) && unsafe && !running);
    p=fresh();start_refuses=1;
    assert(!controller(&p) && unsafe && !running);
    p=fresh();
    /* Already companion-owned keyboard stays untouched during startup too. */
    regs[(OP+0x44-BASE)/4]=1|(1u<<13);
    assert(controller(&p) && running && !unsafe);
    initialized=1;
    unsigned before=writes;
    assert(!usb_ehci_storage_scan() && writes==before);
    assert(!usb_ehci_storage_scan() && writes==before);
    /* Eject operates on storage port 2, not companion keyboard port 1. */
    disks[0]=(device_t){.port=2,.address=1,.in=1,.out=2};
    regs[(OP+0x48-BASE)/4]=5;
    assert(usb_ehci_storage_present(0));
    assert(!usb_ehci_storage_release(0) && !usb_ehci_storage_present(0));
    assert(port_writes==1 && (rd(OP+0x44)&(1u<<13)));
    /* Cannot reuse a QH/qTD after a failed async-disable handshake. */
    regs[(OP+4-BASE)/4]|=1u<<15;retire_refuses=1;
    head.token=0x12345678;td[0].token=0x87654321;
    assert(stop_schedule()==-1 && unsafe);
    before=writes;
    assert(transfer(1,1,1)==-1 && writes==before);
    assert(head.token==0x12345678 && td[0].token==0x87654321);
    assert(usb_ehci_storage_scan()==-1 && writes==before);
    puts("PASS: EHCI BIOS refusal, malformed capabilities/BARs, bounded startup, "
         "companion isolation, targeted eject, no rescan reset, DMA quarantine");
}
