#ifndef MSDOS_EMULATOR_H
#define MSDOS_EMULATOR_H
#include <stdint.h>
int msdos_run(int argc, const char *argv[]);
/* Internal platform hooks for the isolated 8086 interpreter. */
int msdos_pc_poll(const uint8_t *ram, const uint8_t *ports);
int msdos_pc_key(void);
void msdos_pc_putchar(uint8_t c);
void msdos_pc_rtc(uint8_t *destination);
int msdos_pc_disk(int drive, uint32_t offset, uint8_t *data,
                  uint32_t length, int write);
int msdos_cpu_run(uint32_t sectors);
void msdos_devices_update(const uint8_t *ram, const uint8_t *ports);
void msdos_devices_restore(void);
int msdos_devices_graphics(void);
int msdos_media_changed(void);
#endif
