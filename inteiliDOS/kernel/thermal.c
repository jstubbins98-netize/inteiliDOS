/* Monitoring does not replace firmware's independent thermal protection. */
#include "thermal.h"
#include "thermal_hw.h"

static volatile thermal_snapshot_t live;
static uint32_t deadline;
static int supported, pending;

void thermal_init(void) {
    live = (thermal_snapshot_t){0};
    supported = thermal_hw_init();
    live.status = supported ? THERMAL_WAITING : THERMAL_UNAVAILABLE;
    live.sensor = "Unavailable";
    live.cpu_channel = thermal_hw_cpu_channel();
    deadline = 0;
    pending = 0;
}

void thermal_tick(uint32_t now) {
    if (pending) {
        int result = thermal_hw_step(now);
        if (result) {
            pending = 0;
            if (result < 0) {
                live.status = THERMAL_READ_ERROR;
                /* Never clear a temperature warning on a failed reading. */
            } else {
                thermal_snapshot_t sample = {0};
                thermal_hw_sample(&sample);
                int valid = 0, hot = 0, cool = 1;
                for (unsigned i = 0; i < 3; i++) {
                    live.temperature_valid[i] = sample.temperature_valid[i];
                    live.temperatures[i] = sample.temperatures[i];
                    live.fan_valid[i] = sample.fan_valid[i];
                    live.fan_rpm[i] = sample.fan_rpm[i];
                    if (sample.temperature_valid[i]) {
                        valid++;
                        hot |= sample.temperatures[i] >= THERMAL_WARN_C;
                        cool &= sample.temperatures[i] <= THERMAL_CLEAR_C;
                    }
                }
                live.sensor = sample.sensor;
                live.samples++;
                live.last_sample_ms = now;
                live.status = THERMAL_OK;
                if (hot) live.cooling_needed = 1;
                /* Missing channels are not evidence that hardware cooled. */
                else if (valid == 3 && cool) live.cooling_needed = 0;
            }
        }
    }
    /* Unsigned subtraction handles the PIT counter wrapping at ~49 days.
     * Schedule from now rather than doing a burst of missed samples. */
    if ((int32_t)(now - deadline) >= 0) {
        deadline = now + THERMAL_INTERVAL_MS;
        live.checks++;
        if (supported && !pending) {
            int rc=thermal_hw_start(now);
            if (rc>0) pending = 1;
            else if (!rc) live.status = THERMAL_READ_ERROR;
        }
    }
}

void thermal_snapshot(thermal_snapshot_t *out) {
    if (!out) return;
#ifdef THERMAL_TEST
    *out = live;
#else
    uint32_t flags;
    __asm__ volatile("pushfl; popl %0; cli" : "=r"(flags) :: "memory");
    *out = live;
    __asm__ volatile("pushl %0; popfl" :: "r"(flags) : "memory", "cc");
#endif
}
