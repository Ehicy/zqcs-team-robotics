#ifndef CRTC_LOWER_MOTOR_HAL_H
#define CRTC_LOWER_MOTOR_HAL_H

#include <stdint.h>

#define LOWER_MOTOR_PWM_HZ 20000U
#define LOWER_MOTOR_PWM_LIMIT 250
#define LOWER_MOTOR_WATCHDOG_MS 50U

/* Main-context initialization; STBY is held low throughout. Initialize this
 * before delay_init(), then explicitly feed the watchdog from the main loop.
 * This HAL belongs only to the lower-motor target, not the historical TT car.
 * An external pull-down on the shared STBY net is required during reset,
 * including IWDG resets, when the MCU pin returns to a high-impedance input. */
void lower_motor_init(void);

/* Signed duty in permille, -250..250. Invalid input or inactive -> all off.
 * A zero wheel coasts even while other wheels share an asserted STBY.
 * A latched watchdog fault cannot be cleared by writing or feeding. */
void lower_motor_write(const int16_t pwm[4], uint8_t active);
void lower_motor_emergency_stop(void);
uint8_t lower_motor_is_enabled(void);

/* Raw, uncalibrated wrapping counts: M1..3 x4; M4 x2. Call at least every
 * 10 ms; any hardware counter delta must be less than 32768 counts/sample.
 * No encoder presence, polarity, wheel geometry or speed is inferred.
 * Returns a sample-valid bitmask (15 on success; 0 if unavailable). */
uint8_t lower_encoder_sample(int32_t counts[4]);

/* Main context with interrupts enabled only. clear() succeeds only with all
 * outputs disabled and restarts the software deadline. A failed IWDG init
 * cannot be cleared without a successful reboot/init. feed() keeps diagnostics
 * alive but never releases a fault; it refuses feeding with interrupts masked. */
void lower_motor_watchdog_feed(void);
uint8_t lower_motor_watchdog_fault(void);
void lower_motor_watchdog_clear(void);

#endif
