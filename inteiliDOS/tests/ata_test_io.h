#ifndef ATA_TEST_IO_H
#define ATA_TEST_IO_H
static void outb(uint16_t port, uint8_t value);
static uint8_t inb(uint16_t port);
static uint16_t inw(uint16_t port);
static void outw(uint16_t port, uint16_t value);
#endif
