#ifndef CRTC_SPEED_CONTROL_H
#define CRTC_SPEED_CONTROL_H
#include <stdint.h>
#include "board_link_config.h"
#ifdef __cplusplus
extern "C" {
#endif

#define SPEED_WHEELS 4U
#define SPEED_SAMPLE_MS 10U
#define SPEED_MAX_UPDATE_GAP_MS 50U
#define SPEED_MAX_TTL_MS 200U
#define SPEED_PWM_LIMIT LINK_SPEED_PWM_LIMIT_PERMILLE
#define SPEED_REVERSE_HOLD_MS 50U
#define SPEED_STALL_MS 500U
#define SPEED_DIRECTION_MS 100U
#define SPEED_STOPPED_MM_S 5
#define SPEED_D_FILTER_MS 30U /* derivative-on-measurement low-pass time constant */

enum {
    SPEED_FAULT_NONE=0, SPEED_FAULT_UNCALIBRATED=1,
    SPEED_FAULT_ENCODER=2, SPEED_FAULT_STALL=3,
    SPEED_FAULT_DIRECTION=4, SPEED_FAULT_TIMING=5,
    /* Published by the STM32 owner, not generated/cleared by the PID core. */
    SPEED_FAULT_WATCHDOG=6
};

#ifdef CRTC_PA_CHASSIS
enum {
    SPEED_ASSIST_FEEDFORWARD=0, SPEED_ASSIST_OWN=1,
    SPEED_ASSIST_SAME_SIDE=2, SPEED_ASSIST_STRAIGHT_CROSS=3
};
#define SPEED_ASSIST_CORRECTION_MAX 250
#define SPEED_ASSIST_NO_PROGRESS_MS 250U
#endif

typedef struct {
    uint32_t counts_per_rev;
    uint16_t diameter_tenth_mm, kp_milli, ki_milli, kd_milli, max_mm_s, accel_mm_s2;
    int8_t motor_sign, encoder_sign;
    uint8_t confirmed;
} speed_config_t;

typedef struct {
    speed_config_t config[SPEED_WHEELS];
    int16_t measured_mm_s[SPEED_WHEELS], ramped_mm_s[SPEED_WHEELS];
    int16_t pwm_permille[SPEED_WHEELS];
    uint8_t active, fault, ready_mask, pid_initialized;
    /* Internal owner-loop state; no dynamic allocation, floating point or I/O. */
    uint32_t last_update_ms, last_sample_ms;
    uint32_t last_counts[SPEED_WHEELS], zero_since_ms[SPEED_WHEELS];
    uint32_t progress_ms[SPEED_WHEELS], direction_ms[SPEED_WHEELS];
    int32_t ramp_milli[SPEED_WHEELS];
    int64_t integral_micro[SPEED_WHEELS];
    int32_t derivative_milli[SPEED_WHEELS];
    int16_t derivative_measurement[SPEED_WHEELS];
    uint8_t derivative_valid[SPEED_WHEELS];
    int8_t last_direction[SPEED_WHEELS];
    uint8_t tracking_stall[SPEED_WHEELS], tracking_direction[SPEED_WHEELS];
    uint8_t feedback_valid_mask;
    /* First encoder fault snapshot for diagnosis; STOP does not overwrite it.
     * Raw signed deltas precede encoder_sign, so physical wiring stays visible. */
    uint8_t fault_encoder_mask;
    uint32_t fault_sample_ms, fault_sample_dt;
    int32_t fault_raw_delta[SPEED_WHEELS];
#ifdef CRTC_PA_CHASSIS
    /* Adaptive PWM assistance is independent of the legacy all-wheel PID.
     * quality is an engineering weight, NOT a fault probability. True encoder
     * observations stay in measured_mm_s/feedback_valid_mask; replacements
     * used for control are explicitly separate. No substitution is odometry. */
    int16_t assist_control_mm_s[4], assist_correction_permille[4];
    uint16_t assist_quality[4], assist_own_quality[4];
    uint8_t assist_source[4];
    uint32_t assist_last_sample_ms, assist_last_counts[4];
    uint32_t assist_progress_ms[4], assist_direction_ms[4];
    int16_t assist_previous_velocity[4], assist_previous_target[4];
    int16_t assist_derivative_measurement[4];
    int32_t assist_derivative_milli[4];
    int64_t assist_integral_micro[4];
    uint8_t assist_window_mask, assist_rebase_mask, assist_seen_mask;
    uint8_t assist_derivative_valid[4], assist_previous_source[4];
#endif
} speed_control_t;

/* Defaults are provisional, NOT physical calibration: 13 single-channel
 * periods/motor rev, 34:1 => {1768,1768,1768,884} counts/wheel rev (x4/x2),
 * diameter 65mm, Kp=1, Ki=2, Kd=0, max=300mm/s, accel=500mm/s^2.
 * confirmed=0. Historical PID starts uninitialized until ESP32 config succeeds;
 * CRTC_PA_CHASSIS starts with active LINK_PID_* defaults and still accepts C3
 * updates. Initialization does not grant a motion lease or confirm calibration.
 * Pass the current continuous raw encoder counts. No encoder counter is reset. */
void speed_control_init(speed_control_t *state, uint32_t now, const int32_t counts[4]);
/* Only while outputs/ramp are zero, all feedback valid and measured speed <=5.
 * Caller must keep sampling stopped wheels; stale feedback is not a physical
 * standstill confirmation. Integers only: CPR 1..1000000, diameter 1..5000
 * tenths-mm, gains <=20000 (not both zero), max 1..1000, accel 1..5000,
 * signs +/-1, confirmed 0/1. Once initialized, gains must match the unified
 * PID transaction; per-wheel calibration cannot change them. Returns 1 on success. */
int speed_control_configure(speed_control_t *state, unsigned wheel,
                            const speed_config_t *config);
/* One shared coefficient set from ESP32; all four histories remain independent.
 * Atomic, stopped-only application. Boot defaults never authorize motion. */
int speed_control_configure_pid(speed_control_t *state, uint16_t kp_milli,
                                uint16_t ki_milli, uint16_t kd_milli);
/* Immediate coast, ramp/integral clear; retain count/time sampling baseline,
 * measured feedback, last direction, and latched fault. */
void speed_control_stop(speed_control_t *state, uint32_t now);
/* Only while stopped with valid near-zero measured feedback. */
int speed_control_clear_fault(speed_control_t *state);
/* Single owner. Update continuously, including while disabled/faulted. The
 * 10ms sample uses actual elapsed time, modular 32-bit encoder deltas, and the
 * ORIGINAL command_ms/TTL. Zero, disabled, expired or invalid requests coast.
 * No floating point. The PID clamps to +/-SPEED_PWM_LIMIT and cannot reverse PWM against
 * its ramped target; reversal first ramps to zero then coasts for 50ms.
 * The output already includes motor_sign and goes directly to the HAL; do NOT
 * apply a second motor_control slew limiter. All four configs must be confirmed.
 * Return 1 for a valid enabled request, else 0. active means nonzero PWM. */
int speed_control_update(speed_control_t *state, uint32_t now,
                         const int32_t counts[4], uint8_t valid_mask,
                         const int16_t targets_mm_s[4], uint8_t enabled,
                         uint32_t command_ms, uint16_t ttl_ms);

#ifdef CRTC_PA_CHASSIS
/* Always-on feedback assistance for the CURRENT PA four-rubber-wheel PWM
 * route. feedforward_pwm is physical polarity, already ramped/reversed by
 * motor_control. Output pwm_permille is physical polarity too: do not negate
 * it again. Reference speed = logical PWM * config.max_mm_s / 1000, where max
 * is the provisional FULL-PWM speed model, not an independently measured fact.
 * Sample every 10ms even when disabled. One invalid wheel NEVER stops others.
 * Own observation first, same-side replacement next; only nearly straight
 * equal-direction requests admit low-weight opposite-side replacements.
 * No trusted observation => unchanged feedforward. Missing PID/configuration
 * also leaves feedforward available. Replacements never integrate error or
 * become true measured telemetry. Recovery/corrections ramp gradually; a
 * correction never reverses a nonzero FF or creates output from zero FF.
 * STOP/disabled/expired commands still output zero. No fault/clear/unlock
 * transaction is added. Return 1 for a current enabled command, otherwise 0. */
int speed_control_assist_pwm(speed_control_t *state, uint32_t now,
                            const int32_t counts[4], uint8_t valid_mask,
                            const int16_t feedforward_pwm[4], uint8_t enabled,
                            uint32_t command_ms, uint16_t ttl_ms);
#endif

#ifdef __cplusplus
}
#endif
#endif
