#ifndef CRTC_LOWER_MOTOR_HAL_H
#define CRTC_LOWER_MOTOR_HAL_H

#include <stdint.h>
#include "board_link_config.h"

#define LOWER_MOTOR_PWM_HZ 20000U
#define LOWER_MOTOR_PWM_LIMIT LINK_SPEED_PWM_LIMIT_PERMILLE
#if LOWER_MOTOR_PWM_LIMIT < LINK_PWM_LIMIT_PERMILLE || LOWER_MOTOR_PWM_LIMIT > 1000
#error Invalid lower-motor PWM ceiling
#endif
#define LOWER_MOTOR_WATCHDOG_MS 50U

/* Main-context initialization; STBY is held low throughout. Initialize this
 * before delay_init(), then explicitly feed the watchdog from the main loop.
 * This HAL belongs only to the lower-motor target, not the historical TT car.
 * An external pull-down on the shared STBY net is required during reset,
 * including IWDG resets, when the MCU pin returns to a high-impedance input. */
void lower_motor_init(void);
/* Once after delay_init() establishes DWT; starts M4 x2 pulse qualification.
 * Never call this to clear a live encoder fault or reset counts in service. */
void lower_encoder_start(void);
#ifdef CRTC_PA_CHASSIS
void lower_encoder_reset(void); /* Reset logical count origin, not IRQ/timer state. */
#endif

/* Signed duty in permille, +/-LOWER_MOTOR_PWM_LIMIT. Invalid input or inactive -> all off.
 * A zero wheel coasts even while other wheels share an asserted STBY.
 * A latched watchdog fault cannot be cleared by writing or feeding. */
void lower_motor_write(const int16_t pwm[4], uint8_t active);
#ifdef CRTC_PA_CHASSIS
/* Selected zero-output wheels short both TB6612 outputs during a bounded
 * brake window. Mask is supplied by the owner, never inferred from zero PWM. */
void lower_motor_write_braked(const int16_t pwm[4], uint8_t active, uint8_t short_brake_mask);
#endif
void lower_motor_emergency_stop(void);
uint8_t lower_motor_is_enabled(void);

/* Raw, uncalibrated wrapping counts: M1..3 x4; M4 x2. Call at least every
 * 10 ms; any hardware counter delta must be less than 32768 counts/sample.
 * No encoder presence, polarity, wheel geometry or speed is inferred.
 * Returns a sample-valid bitmask (15 on success; M4 bit clears on IRQ storm). */
uint8_t lower_encoder_sample(int32_t counts[4]);

/* Main context with interrupts enabled only. clear() succeeds only with all
 * outputs disabled and restarts the software deadline. A failed IWDG init
 * cannot be cleared without a successful reboot/init. feed() keeps diagnostics
 * alive but never releases a fault; it refuses feeding with interrupts masked. */
void lower_motor_watchdog_feed(void);
uint8_t lower_motor_watchdog_fault(void);
void lower_motor_watchdog_clear(void);

#endif
