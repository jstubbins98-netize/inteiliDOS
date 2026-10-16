#ifndef BATTERY_H
#define BATTERY_H
#include <stdint.h>
enum { BATTERY_UNAVAILABLE, BATTERY_WAITING, BATTERY_OK, BATTERY_READ_ERROR };
typedef struct {
    unsigned status, percent_valid, health_valid;
    unsigned percent, health_percent, full_capacity, design_capacity;
    unsigned discharging, fully_charged, capacity_energy;
    uint32_t sampled_ms;
} battery_snapshot_t;
void battery_init(void);
/* Nonblocking, read-only Smart Battery polling; call from the health loop. */
void battery_poll(uint32_t now);
void battery_snapshot(battery_snapshot_t *out);
void battery_shutdown(void);
#endif
