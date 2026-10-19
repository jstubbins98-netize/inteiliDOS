#ifndef THERMAL_HW_H
#define THERMAL_HW_H
#include "thermal.h"

int thermal_hw_init(void);
int thermal_hw_start(uint32_t now_ms);
/* Reserve an idle shared SMBus for read-only battery transactions.
 * Call with host interrupts briefly disabled; release on completion. */
int thermal_hw_bus_claim(uint16_t io_base);
void thermal_hw_bus_release(void);
/* 0: pending; 1: complete; -1: failed. No waits or heap allocations. */
int thermal_hw_step(uint32_t now_ms);
void thermal_hw_sample(thermal_snapshot_t *out);
uint8_t thermal_hw_cpu_channel(void);
#endif
