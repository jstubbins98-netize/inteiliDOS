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
#define KEY_F8        0x97

void keyboard_init(void);
int  keyboard_getchar(void);     /* blocking read */
int  keyboard_poll(void);        /* non-blocking; -1 if no key */

#endif /* KEYBOARD_H */
