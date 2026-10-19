#ifndef KEYBOARD_H
#define KEYBOARD_H

#include <stdint.h>

#define KEY_BACKSPACE 0x08
#define KEY_ENTER     0x0D
#define KEY_ESCAPE    0x1B
#define KEY_UP        0x80
#define KEY_DOWN      0x81
#define KEY_LEFT      0x82
#define KEY_RIGHT     0x83
#define KEY_F1        0x90
#define KEY_F2        0x91
#define KEY_F3        0x92
#define KEY_F4        0x93
#define KEY_F5        0x94
#define KEY_F6        0x95
#define KEY_F7        0x96
#define KEY_F8        0x97
#define KEY_F9        0x98
#define KEY_F10       0x99
#define KEY_F11       0x9A
#define KEY_F12       0x9B

void keyboard_init(void);
int  keyboard_getchar(void);     /* blocking read */
int  keyboard_poll(void);        /* non-blocking; -1 if no key */

/* Inject a character into the shared keyboard buffer.
 * Called by the USB HID driver to feed keys through the same path
 * as PS/2, so keyboard_getchar() / keyboard_poll() serve both.         */
void keyboard_inject(uint8_t c);

#endif /* KEYBOARD_H */
