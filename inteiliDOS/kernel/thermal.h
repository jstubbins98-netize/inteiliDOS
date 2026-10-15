#ifndef THERMAL_H
#define THERMAL_H
#include <stdint.h>

#define THERMAL_INTERVAL_MS 500u
/* Advisory defaults, NOT OEM-certified trip points or shutdown limits. */
#define THERMAL_WARN_C 75
#define THERMAL_CLEAR_C 65

typedef enum {
    THERMAL_UNAVAILABLE, THERMAL_WAITING, THERMAL_OK, THERMAL_READ_ERROR
} thermal_status_t;

typedef struct {
    thermal_status_t status;
    uint32_t checks, samples, last_sample_ms;
    int temperatures[3];
    uint8_t temperature_valid[3], fan_valid[3];
    uint32_t fan_rpm[3];
    uint8_t cpu_channel; /* 0: wiring unknown; otherwise 1..3 */
    uint8_t cooling_needed;
    const char *sensor;
} thermal_snapshot_t;

void thermal_init(void);
/* Bounded, non-blocking background work, called from the host timer IRQ. */
void thermal_tick(uint32_t now_ms);
void thermal_snapshot(thermal_snapshot_t *out);
#endif
