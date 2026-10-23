#ifndef CRTC_FALLBACK_HAL_CLOCK_H
#define CRTC_FALLBACK_HAL_CLOCK_H
#include <stdint.h>
/* Minimal interface for the emergency targets. The legacy lower-motor header
 * and implementation retain encoder/PID APIs for the old target only. */
void lower_motor_init(void);
void lower_motor_write(const int16_t pwm[4], uint8_t active);
void lower_motor_emergency_stop(void);
uint8_t lower_motor_is_enabled(void);
void lower_motor_watchdog_feed(void);
uint8_t lower_motor_watchdog_fault(void);
void lower_motor_watchdog_clear(void);
#endif
