#ifndef USB_EHCI_H
#define USB_EHCI_H
#include <stdint.h>
typedef struct {
    const char *status;
    uint16_t vendor_id, device_id;
    uint8_t bus, device, function, found;
    unsigned connected_ports, failed_ports;
} usb_ehci_info_t;
const usb_ehci_info_t *usb_ehci_info(void);
int usb_ehci_init(void);
int usb_ehci_storage_scan(void);
void usb_ehci_storage_forget_absent(void);
int usb_ehci_storage_present(unsigned disk);
int usb_ehci_storage_command(unsigned disk,const uint8_t *cdb,unsigned len,
                             uint8_t *buffer,unsigned bytes,int write);
int usb_ehci_storage_release(unsigned disk);
#endif
