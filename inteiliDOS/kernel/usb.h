/*
 * inteilidOS -- kernel/usb.h
 * USB HID keyboard driver (UHCI host controller, boot protocol)
 */

#ifndef USB_H
#define USB_H

/* Probe PCI bus for a UHCI controller, enumerate any attached USB keyboard,
 * and start polling.  Safe to call when no USB hardware is present — the
 * function returns silently without affecting PS/2 keyboard operation.
 * Requires interrupts to be enabled (sti) before calling.               */
void usb_keyboard_init(void);

#endif /* USB_H */
