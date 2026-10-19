#ifndef BATTERY_TEST_IO_H
#define BATTERY_TEST_IO_H
static uint8_t inb(uint16_t port);
static void outb(uint16_t port,uint8_t value);
static uint32_t irq_lock(void);
static void irq_unlock(uint32_t flags);
#endif
