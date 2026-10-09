#ifndef INTEILIDOS_DOS_INTERNAL_H
#define INTEILIDOS_DOS_INTERNAL_H
#include "dos.h"
#define DOS_PSP 0x2000u
#define DOS_TOP 0xA000u
#define DOS_VM 0x20000u
/* Additional segment words present only in CPU v86 interrupt frames. */
typedef struct {
    registers_t r;
    uint32_t ves, vds, vfs, vgs;
} dos_frame_t;
uint8_t *dos_pointer(uint16_t seg, uint16_t off, uint32_t size, int write);
void dos_stop(registers_t *r, int result, const char *reason);
void dos_services_init(const dos_filesystem_t *fs);
int dos_interrupt(dos_frame_t *f, uint8_t number);
int dos_key(int wait);
int dos_key_peek(void);
uint16_t dos_virtual_flags(registers_t *r);
void dos_virtual_interrupts(int enabled);
uint16_t dos_allocate(uint16_t count, uint16_t *largest);
int dos_resize(uint16_t seg, uint16_t count, uint16_t *largest);
int dos_free(uint16_t seg);
extern uint8_t dos_status;
extern int dos_cancelled;
#endif
