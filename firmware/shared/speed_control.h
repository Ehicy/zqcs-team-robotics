#ifndef CRTC_SPEED_CONTROL_H
#define CRTC_SPEED_CONTROL_H
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif

#define SPEED_WHEELS 4U
#define SPEED_SAMPLE_MS 10U
#define SPEED_MAX_UPDATE_GAP_MS 50U
#define SPEED_MAX_TTL_MS 200U
#define SPEED_PWM_LIMIT 250
#define SPEED_REVERSE_HOLD_MS 50U
#define SPEED_STALL_MS 500U
#define SPEED_DIRECTION_MS 100U
#define SPEED_STOPPED_MM_S 5

enum {
    SPEED_FAULT_NONE=0, SPEED_FAULT_UNCALIBRATED=1,
    SPEED_FAULT_ENCODER=2, SPEED_FAULT_STALL=3,
    SPEED_FAULT_DIRECTION=4, SPEED_FAULT_TIMING=5,
    /* Published by the STM32 owner, not generated/cleared by the PI core. */
    SPEED_FAULT_WATCHDOG=6
};

typedef struct {
    uint32_t counts_per_rev;
    uint16_t diameter_tenth_mm, kp_milli, ki_milli, max_mm_s, accel_mm_s2;
    int8_t motor_sign, encoder_sign;
    uint8_t confirmed;
} speed_config_t;

typedef struct {
    speed_config_t config[SPEED_WHEELS];
    int16_t measured_mm_s[SPEED_WHEELS], ramped_mm_s[SPEED_WHEELS];
    int16_t pwm_permille[SPEED_WHEELS];
    uint8_t active, fault, ready_mask;
    /* Internal owner-loop state; no dynamic allocation, floating point or I/O. */
    uint32_t last_update_ms, last_sample_ms;
    uint32_t last_counts[SPEED_WHEELS], zero_since_ms[SPEED_WHEELS];
    uint32_t progress_ms[SPEED_WHEELS], direction_ms[SPEED_WHEELS];
    int32_t ramp_milli[SPEED_WHEELS];
    int64_t integral_micro[SPEED_WHEELS];
    int8_t last_direction[SPEED_WHEELS];
    uint8_t tracking_stall[SPEED_WHEELS], tracking_direction[SPEED_WHEELS];
    uint8_t feedback_valid_mask;
} speed_control_t;

/* Defaults are provisional, NOT physical calibration: 13 single-channel
 * periods/motor rev, 34:1 => {1768,1768,1768,884} counts/wheel rev (x4/x2),
 * diameter 65mm, Kp=1, Ki=2, max=300mm/s, accel=500mm/s^2. confirmed=0.
 * Pass the current continuous raw encoder counts. No encoder counter is reset. */
void speed_control_init(speed_control_t *state, uint32_t now, const int32_t counts[4]);
/* Only while outputs/ramp are zero, all feedback valid and measured speed <=5.
 * Caller must keep sampling stopped wheels; stale feedback is not a physical
 * standstill confirmation. Integers only: CPR 1..1000000, diameter 1..5000
 * tenths-mm, gains <=20000 (not both zero), max 1..1000, accel 1..5000,
 * signs +/-1, confirmed 0/1. Returns 1 on accepted configuration. */
int speed_control_configure(speed_control_t *state, unsigned wheel,
                            const speed_config_t *config);
/* Immediate coast, ramp/integral clear; retain count/time sampling baseline,
 * measured feedback, last direction, and latched fault. */
void speed_control_stop(speed_control_t *state, uint32_t now);
/* Only while stopped with valid near-zero measured feedback. */
int speed_control_clear_fault(speed_control_t *state);
/* Single owner. Update continuously, including while disabled/faulted. The
 * 10ms sample uses actual elapsed time, modular 32-bit encoder deltas, and the
 * ORIGINAL command_ms/TTL. Zero, disabled, expired or invalid requests coast.
 * No floating point. The PI clamps to +/-250 and cannot reverse PWM against
 * its ramped target; reversal first ramps to zero then coasts for 50ms.
 * The output already includes motor_sign and goes directly to the HAL; do NOT
 * apply a second motor_control slew limiter. All four configs must be confirmed.
 * Return 1 for a valid enabled request, else 0. active means nonzero PWM. */
int speed_control_update(speed_control_t *state, uint32_t now,
                         const int32_t counts[4], uint8_t valid_mask,
                         const int16_t targets_mm_s[4], uint8_t enabled,
                         uint32_t command_ms, uint16_t ttl_ms);

#ifdef __cplusplus
}
#endif
#endif
