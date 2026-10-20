#ifndef MOUSE_INPUT_H
#define MOUSE_INPUT_H
#include <stdint.h>
/* Session-scoped input: prefer direct-root xHCI, then UHCI boot mouse, else PS/2.
 * start requires IRQs enabled; no guest code accesses native USB/8042 I/O. */
int mouse_input_start(void);
void mouse_input_stop(void);
void mouse_input_byte(uint8_t value);
void mouse_input_poll(int *dx, int *dy, unsigned *buttons);
#endif
