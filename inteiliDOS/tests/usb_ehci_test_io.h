/* Host register model; production uses real MMIO and timer IRQ waits. */
static uint32_t rd(uint32_t p);
static void wr(uint32_t p,uint32_t v);
static void idle(void);
