#ifndef USB_TRANSPORT_H
#define USB_TRANSPORT_H
#include <stdint.h>
int usb_transport_scan(void);
/* Call only after all old filesystem mounts have been flushed/unmounted. */
void usb_transport_forget_absent(void);
int usb_transport_present(unsigned disk);
int usb_transport_command(unsigned disk,const uint8_t *cdb,unsigned len,
                          uint8_t *buffer,unsigned bytes,int write);
int usb_transport_release(unsigned disk);
#endif
