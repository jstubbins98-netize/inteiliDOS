/*
 * inteilidOS -- kernel/usb.h
 * USB HID boot keyboard/mouse input on direct UHCI / xHCI root ports.
 */

#ifndef USB_H
#define USB_H
#include <stdint.h>

typedef struct {
    uint8_t configuration, interface, endpoint, interval;
    uint16_t packet_size;
} usb_keyboard_descriptor_t;
int usb_parse_boot_keyboard(const uint8_t *buf, unsigned length,
                            usb_keyboard_descriptor_t *out);
/* protocol 1 = keyboard (8 bytes), 2 = mouse (3 bytes); alternate zero only. */
int usb_parse_boot_hid(const uint8_t *buf, unsigned length, unsigned protocol,
                       usb_keyboard_descriptor_t *out);
int usb_process_boot_report(const uint8_t current[8], const uint8_t previous[8]);
const char *usb_keyboard_status(void);
/* Session consumption only; native boot owns enumeration and the shared
 * schedule. Call with interrupts masked. No hub, wheel or hotplug support. */
int usb_uhci_mouse_start(void);
void usb_uhci_mouse_stop(void);
void usb_uhci_mouse_poll(int *dx,int *dy,unsigned *buttons);

/* Probe PCI for supported controllers, enumerate a direct USB keyboard,
 * and start polling.  Safe to call when no USB hardware is present — the
 * function returns silently without affecting PS/2 keyboard operation.
 * Requires interrupts to be enabled (sti) before calling.               */
void usb_keyboard_init(void);

#endif /* USB_H */
