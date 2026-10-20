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
/* Shared 8042 output: status bit 5 distinguishes AUX from keyboard bytes. */
void keyboard_ps2_input(uint8_t status, uint8_t value);
int  keyboard_getchar(void);     /* blocking read */
int  keyboard_poll(void);        /* non-blocking; -1 if no key */

/* Modifier bits match the USB HID boot report (left/right remain distinct). */
#define KEY_MOD_LCTRL  0x01
#define KEY_MOD_LSHIFT 0x02
#define KEY_MOD_LALT   0x04
#define KEY_MOD_RCTRL  0x10
#define KEY_MOD_RSHIFT 0x20
#define KEY_MOD_RALT   0x40
#define KEY_MOD_SHIFT (KEY_MOD_LSHIFT | KEY_MOD_RSHIFT)
#define KEY_MOD_CTRL  (KEY_MOD_LCTRL | KEY_MOD_RCTRL)
#define KEY_MOD_ALT   (KEY_MOD_LALT | KEY_MOD_RALT)

typedef struct {
    uint8_t character;           /* unchanged legacy keyboard_poll value */
    uint8_t scancode;            /* set-1 make code; zero for state-only */
    uint8_t modifiers;           /* snapshot at the time of this event */
    uint8_t caps_lock;
} keyboard_event_t;

/* Consumes the SAME queue as keyboard_poll, including modifier-only events.
 * Legacy readers skip those events, so native shortcuts/typing stay intact. */
int keyboard_poll_event(keyboard_event_t *event);
void keyboard_inject_event(uint8_t character, uint8_t scancode,
                           uint8_t modifiers, uint8_t caps_lock);
void keyboard_usb_modifiers(uint8_t modifiers, uint8_t caps_lock);
void keyboard_current_event(keyboard_event_t *event);

/* Inject a character into the shared keyboard buffer.
 * Called by the USB HID driver to feed keys through the same path
 * as PS/2, so keyboard_getchar() / keyboard_poll() serve both.         */
void keyboard_inject(uint8_t c);

#endif /* KEYBOARD_H */
