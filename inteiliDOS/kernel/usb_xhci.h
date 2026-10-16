#ifndef USB_XHCI_H
#define USB_XHCI_H
#include <stdint.h>
int usb_xhci_init(void);
void usb_xhci_poll(void);
typedef struct {
    const char *status;
    uint16_t vendor_id, device_id;
    uint8_t bus, device, function, found;
    unsigned connected_ports, failed_ports, intel_routing;
    unsigned completion_code, event_type;
} usb_xhci_info_t;
const usb_xhci_info_t *usb_xhci_info(void);
/* Synchronous, LUN-0 SCSI bulk-only storage on direct xHCI root ports.
 * No calls from IRQ context; all DMA remains in permanently owned buffers. */
int usb_xhci_storage_scan(void);
int usb_xhci_storage_present(unsigned disk);
int usb_xhci_storage_command(unsigned disk, const uint8_t *cdb, unsigned cdb_len,
                             uint8_t *data, unsigned bytes, int write);
int usb_xhci_storage_release(unsigned disk);
#endif
