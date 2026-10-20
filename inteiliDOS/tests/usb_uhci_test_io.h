/* Host model hooks; production uses real port I/O. */
static uint16_t inw(uint16_t port);
static void outw(uint16_t port,uint16_t value);
static void outb(uint16_t port,uint8_t value);
static void outl(uint16_t port,uint32_t value);
static void io_wait(void);
