/* Stable volume-to-controller bindings: never move a still-bound volume when
 * another controller appears. Failed bindings remain until explicit remount. */
#include "usb_transport.h"
#include "usb_xhci.h"
#include "usb_ehci.h"
static struct { unsigned kind, disk; } bindings[4];
static int present(unsigned kind,unsigned disk) {
    return kind==1 ? usb_xhci_storage_present(disk) : usb_ehci_storage_present(disk);
}
void usb_transport_forget_absent(void) {
    for (unsigned i=0;i<4;i++)
        if (bindings[i].kind && !present(bindings[i].kind,bindings[i].disk))
            bindings[i].kind=0;
    usb_ehci_storage_forget_absent();
}
int usb_transport_scan(void) {
    int x=usb_xhci_storage_scan(), e=usb_ehci_storage_scan(), count=0;
    for (unsigned kind=1;kind<=2;kind++) for (unsigned disk=0;disk<4;disk++) {
        if (!present(kind,disk)) continue;
        unsigned i;
        for (i=0;i<4;i++) if (bindings[i].kind==kind && bindings[i].disk==disk) break;
        if (i==4) {
            for (i=0;i<4 && bindings[i].kind;i++) {}
            if (i<4) {bindings[i].kind=kind;bindings[i].disk=disk;}
        }
    }
    for (unsigned i=0;i<4;i++) count+=usb_transport_present(i);
    return x<0 && e<0 ? -1 : count;
}
int usb_transport_present(unsigned i) {
    return i<4 && bindings[i].kind && present(bindings[i].kind,bindings[i].disk);
}
int usb_transport_command(unsigned i,const uint8_t *cdb,unsigned len,
                          uint8_t *buffer,unsigned bytes,int write) {
    if (!usb_transport_present(i)) return -1;
    return bindings[i].kind==1 ?
        usb_xhci_storage_command(bindings[i].disk,cdb,len,buffer,bytes,write) :
        usb_ehci_storage_command(bindings[i].disk,cdb,len,buffer,bytes,write);
}
int usb_transport_release(unsigned i) {
    if (i>=4 || !bindings[i].kind) return -1;
    int rc=bindings[i].kind==1 ? usb_xhci_storage_release(bindings[i].disk) :
                               usb_ehci_storage_release(bindings[i].disk);
    if (!rc) bindings[i].kind=0;
    return rc;
}
