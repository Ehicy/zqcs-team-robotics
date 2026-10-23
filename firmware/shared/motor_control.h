#ifndef CRTC_MOTOR_CONTROL_H
#define CRTC_MOTOR_CONTROL_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Open-loop duty requests, NOT wheel speeds or PID targets. */
#ifdef CRTC_PA_CHASSIS
#define MOTOR_MAX_PERMILLE 1000
#else
#define MOTOR_MAX_PERMILLE 250
#endif
#define MOTOR_SLEW_PER_SECOND 1000U
#define MOTOR_REVERSE_HOLD_MS 50U
#define MOTOR_MAX_UPDATE_GAP_MS 50U
#define MOTOR_COMMAND_MAX_TTL_MS 200U
#define MOTOR_WHEEL_COUNT 4U

typedef struct {
    int16_t output_permille[MOTOR_WHEEL_COUNT];
    uint8_t active, fault;
    uint32_t last_update_ms;
    /* Preserve direction and zero-start time across stops, so a disable/enable
     * cycle cannot bypass the reversing dead time. */
    int8_t last_direction[MOTOR_WHEEL_COUNT];
    uint32_t zero_since_ms[MOTOR_WHEEL_COUNT];
} motor_control_t;

void motor_control_init(motor_control_t *state, uint32_t now);
/* Single owner, no hardware I/O. A zero output means coast, not active braking.
 * command_ms is the ORIGINAL source command time; never refresh it in a loop.
 * TTL is valid only for 1..200 ms and expires at age >= TTL (wrap-safe).
 * Invalid/expired/disabled commands stop all wheels immediately. A zero wheel
 * target also coasts immediately. Nonzero reversals ramp down, hold at zero for
 * 50 ms, then ramp up in the new direction. No new-direction ramp time accrues
 * during that hold. Requests outside +/-250 are rejected, not silently clamped.
 * A scheduler gap >50 ms latches fault until an explicit enabled=0 update.
 * Return 1 for an accepted enabled command (including zero targets), else 0.
 * active reports actual nonzero output, not merely an enabled input request. */
int motor_control_update(motor_control_t *state, uint32_t now,
                         const int16_t targets_permille[MOTOR_WHEEL_COUNT],
                         uint8_t enabled, uint32_t command_ms, uint16_t ttl_ms);

#ifdef __cplusplus
}
#endif
#endif
