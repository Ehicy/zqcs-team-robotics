#ifndef CRTC_FALLBACK_H
#define CRTC_FALLBACK_H
#include "servo_motion.h"
#include <stdint.h>
/* Emergency build profile: preserve the old modules, do not compile them.
 * Encoder/PID/BLE/Dashboard sources remain available under the old targets. */
#define FALLBACK_ENCODER_ENABLED 0
#define FALLBACK_PID_ENABLED 0
#define FALLBACK_DASHBOARD_ENABLED 0
#define FALLBACK_ESP32_ENABLED 0
#define FB_CHANNELS 3U
#define FB_PARAM_COUNT 13U
#define FB_INPUT_TTL 120U
/* Disabled after observed restoring torque / reversal across one position.
 * 1500us may be an absolute position, NOT a universal stop command. Retain
 * historical constants only for the excluded continuous-rotation profile. */
#define FB_CONTINUOUS_ARM_PROFILE 0
#define FB_BIG_ARM_STOP_US 1500U
#define FB_BIG_ARM_JOG_US 20U
#define FB_BIG_ARM_BURST_MS 1000U
#define FB_SERVO_PROBE_KEY 0x53000000UL
#define FB_SERVO_PROBE_LEASE_MS 500U
typedef struct {
    uint32_t token, heartbeat_ms, started_ms;
    uint8_t active, blocked, moving;
} fb_servo_probe_t;
/* Legacy continuous-rotation probe. Disabled when profile == 0. */
int fb_servo_probe_poll(fb_servo_probe_t *s, uint32_t token, uint8_t ready,
                        uint8_t stop, uint32_t now);
#define FB_MASK(n) (1U << (n))
enum
{
    FB_SELECT = 0,
    FB_L3,
    FB_R3,
    FB_START,
    FB_UP,
    FB_RIGHT,
    FB_DOWN,
    FB_LEFT,
    FB_L2,
    FB_R2,
    FB_L1,
    FB_R1,
    FB_TRIANGLE,
    FB_CIRCLE,
    FB_CROSS,
    FB_SQUARE
};
enum
{
    FB_LOCKED,
    FB_DRIVE,
    FB_SERVO,
    FB_TUNE
};
typedef struct
{
    uint16_t max_pwm, turn_percent, motor_negative, jog_us;
    uint16_t gain[4], servo_min[3], servo_max[3], servo_reference[3];
    uint16_t ramp_ms, brake_pwm, brake_ms;
} fb_config_t;
typedef struct
{
    uint16_t buttons;
    uint8_t valid, mode, lx, ly, rx, ry;
} fb_input_t;
/* Independent input diagnostic: works before drive unlock / UART readiness. */
typedef struct
{
    uint32_t started_ms;
    uint8_t down, active;
} fb_l3_led_t;
void fb_l3_led_sample(fb_l3_led_t *s, const fb_input_t *p, uint32_t now);
int fb_l3_led_output(fb_l3_led_t *s, uint32_t now); /* -1: normal status LED */
/* Explicit RAM-only ST-Link bench lease. Never enables at boot or auto-restarts. */
#define FB_BENCH_KEY 0xB37C0000UL
#define FB_BENCH_LEASE_MS 750U
#define FB_BENCH_LIMIT_MS 120000U
#define FB_BENCH_PWM 1000 /* per mille: 100% explicitly requested; not measured speed */
enum { FB_BENCH_IDLE, FB_BENCH_RUN, FB_BENCH_DONE, FB_BENCH_EXPIRED,
       FB_BENCH_FAULT, FB_BENCH_STOPPED };
typedef struct
{
    uint32_t started_ms, heartbeat_ms, last_command;
    uint8_t state;
} fb_bench_t;
/* 1 bench drives, 0 force stop, -1 normal remote; command 0 re-arms after stop. */
int fb_bench_poll(fb_bench_t *s, uint32_t command, uint8_t ready, uint8_t stop, uint32_t now);
typedef struct
{
    fb_config_t config;
    servo_motion_t servo[3];
    int16_t wheels[4];
    uint16_t previous_buttons;
    uint32_t sample_ms, neutral_ms, hold_ms, repeat_ms, last_change_ms, last_save_ms;
    uint16_t hold_chord, repeat_key, report_value;
    uint8_t mode, selected, param, armed, neutral_ready, hold_done, save_request;
    uint8_t stop_event, notice, report_request, dirty, have_saved;
} fb_remote_t;
void fb_config_default(fb_config_t *c);
int fb_config_valid(const fb_config_t *c);
void fb_remote_init(fb_remote_t *s, uint32_t now);
void fb_remote_fault(fb_remote_t *s, uint32_t now);
void fb_remote_sample(fb_remote_t *s, const fb_input_t *p, uint8_t link_ready, uint32_t now);
void fb_remote_tick(fb_remote_t *s, uint8_t link_ready, uint32_t now);
uint16_t fb_param_value(const fb_remote_t *s);
int fb_param_adjust(fb_remote_t *s, int direction, uint32_t now);
int fb_servo_target(fb_remote_t *s, unsigned channel, int32_t target, uint32_t now);
int fb_servo_enable(fb_remote_t *s, unsigned channel, uint32_t now);
/* Two-page atomic journal; callbacks are the existing calibration_flash API. */
int fb_store_load(fb_config_t *c);
int fb_store_save(const fb_config_t *c); /* 0 failure, 1 saved, 2 unchanged */

enum
{
    FB_MOTOR_IDLE,
    FB_MOTOR_RAMP,
    FB_MOTOR_RUN,
    FB_MOTOR_REVERSING,
    FB_MOTOR_COAST,
    FB_MOTOR_BRAKE
};
typedef struct
{
    int16_t output[4], origin[4], goal[4], reverse[4];
    uint32_t last_ms, start_ms, stop_ms;
    uint16_t duration_ms, brake_ms;
    uint8_t phase;
} fb_motor_t;
void fb_motor_init(fb_motor_t *m, uint32_t now);
void fb_motor_off(fb_motor_t *m, uint32_t now);
void fb_motor_stop(fb_motor_t *m, uint16_t pwm, uint16_t ms, uint32_t now);
void fb_motor_tick(fb_motor_t *m, const int16_t target[4], uint8_t enabled, uint16_t ramp_ms,
                   uint32_t now);
#endif
