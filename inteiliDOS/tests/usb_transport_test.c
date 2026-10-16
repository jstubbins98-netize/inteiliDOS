/* Mixed-controller bindings must not move mounted volumes during a scan. */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "../kernel/usb_transport.c"
static unsigned x[4],e[4],last_kind,last_disk,release_fail,forgot;
int usb_xhci_storage_present(unsigned i) {return i<4 && x[i];}
int usb_ehci_storage_present(unsigned i) {return i<4 && e[i];}
int usb_xhci_storage_scan(void) {return 0;}
int usb_ehci_storage_scan(void) {return 0;}
void usb_ehci_storage_forget_absent(void) {forgot++;}
int usb_xhci_storage_command(unsigned i,const uint8_t *c,unsigned n,uint8_t *b,unsigned z,int w) {
    (void)c;(void)n;(void)b;(void)z;(void)w;last_kind=1;last_disk=i;return 0;
}
int usb_ehci_storage_command(unsigned i,const uint8_t *c,unsigned n,uint8_t *b,unsigned z,int w) {
    (void)c;(void)n;(void)b;(void)z;(void)w;last_kind=2;last_disk=i;return 0;
}
int usb_xhci_storage_release(unsigned i) {if(release_fail)return -1;x[i]=0;return 0;}
int usb_ehci_storage_release(unsigned i) {if(release_fail)return -1;e[i]=0;return 0;}
int main(void) {
    uint8_t cdb[10]={0x35};
    e[1]=1;
    assert(usb_transport_scan()==1);
    assert(!usb_transport_command(0,cdb,10,0,0,0) && last_kind==2 && last_disk==1);
    x[0]=1;e[3]=1;
    assert(usb_transport_scan()==3);
    assert(bindings[0].kind==2 && bindings[0].disk==1);
    assert(!usb_transport_command(1,cdb,10,0,0,0) && last_kind==1 && last_disk==0);
    assert(!usb_transport_command(2,cdb,10,0,0,0) && last_kind==2 && last_disk==3);
    e[1]=0;x[2]=1;
    assert(usb_transport_scan()==3 && !usb_transport_present(0));
    assert(bindings[0].kind==2); /* keep missing mounted volume, no aliasing */
    release_fail=1;
    assert(usb_transport_release(1)==-1 && bindings[1].kind==1);
    release_fail=0;
    assert(!usb_transport_release(1) && !bindings[1].kind);
    usb_transport_forget_absent();
    assert(forgot==1 && !bindings[0].kind);
    e[0]=1;assert(usb_transport_scan()==3);
    assert(bindings[0].kind==2 && bindings[0].disk==0);
    assert(usb_transport_command(4,cdb,10,0,0,0)==-1);
    assert(usb_transport_release(4)==-1);
    puts("PASS: mixed EHCI/xHCI routing, stable bindings, missing-drive isolation, "
         "failed-release retention and explicit remount");
}
