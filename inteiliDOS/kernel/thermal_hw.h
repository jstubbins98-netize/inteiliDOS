#ifndef THERMAL_HW_H
#define THERMAL_HW_H
#include "thermal.h"

int thermal_hw_init(void);
int thermal_hw_start(uint32_t now_ms);
/* 0: pending; 1: complete; -1: failed. No waits or heap allocations. */
int thermal_hw_step(uint32_t now_ms);
void thermal_hw_sample(thermal_snapshot_t *out);
uint8_t thermal_hw_cpu_channel(void);
#endif
