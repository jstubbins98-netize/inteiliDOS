/* Test-only MMIO and idle hooks; production builds use real MMIO/HLT. */
static uint32_t rd(uint32_t address);
static void wr(uint32_t address,uint32_t value);
static void idle(void);
