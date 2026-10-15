#ifndef THERMAL_TEST_IO_H
#define THERMAL_TEST_IO_H
#include <stdint.h>
static uint8_t inb(uint16_t port);
static void outb(uint16_t port, uint8_t value);
#endif
