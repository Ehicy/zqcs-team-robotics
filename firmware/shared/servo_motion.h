#ifndef CRTC_SERVO_MOTION_H
#define CRTC_SERVO_MOTION_H
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
#define SERVO_MOTION_SPEED_US_S 100U
#define SERVO_MOTION_ACCEL_US_S2 200U
#define SERVO_MOTION_LEASE_MS 500U
#define SERVO_MOTION_MAX_GAP_MS 50U
/* All positions are commanded references, never measured shaft positions. */
typedef struct {
    int32_t position_milli, velocity_milli;
    uint16_t pulse, target;
    uint32_t tick_ms, renewed_ms;
    uint8_t moving;
} servo_motion_t;
void servo_motion_reference(servo_motion_t *s, uint16_t pulse, uint32_t now);
void servo_motion_target(servo_motion_t *s, uint16_t target, uint32_t received_ms);
void servo_motion_freeze(servo_motion_t *s);
/* Updates a candidate only; owner commits it after a successful HAL write. */
void servo_motion_tick(servo_motion_t *s, uint32_t now);
#ifdef __cplusplus
}
#endif
#endif
