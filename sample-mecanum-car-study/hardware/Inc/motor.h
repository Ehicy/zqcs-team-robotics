#ifndef MOTOR_H
#define MOTOR_H
#include <stdint.h>

/* 实测回中范围后再调整；默认 120～136（含端点）均视为回中。 */
#define JOYSTICK_CENTER       128
#define JOYSTICK_DEADZONE     8
#define MOTOR_SPEED_INITIAL_PERCENT 0U
#define MOTOR_SPEED_MAX_PERCENT     100U
#define MOTOR_SPEED_STEP_PERCENT    10U
#define MOTOR_SPEED_LED_THRESHOLD   60U
/* 俯视滚子呈 O/菱形：前后轴距大于左右轮距时取 +1；反之取 -1。
 * 这是旋转方向配置，不是已测得的角速度或几何尺寸。
 */
#define MOTOR_O_ROTATION_SIGN 1.0f

#if JOYSTICK_DEADZONE < 0 || JOYSTICK_DEADZONE >= 127
#error JOYSTICK_DEADZONE must be between 0 and 126
#endif

void motor_init(void);
void motor_stop(void);
void motor_set_speed_percent(uint8_t percent);
uint8_t motor_get_speed_percent(void);
uint8_t motor_joystick_is_centered(uint8_t value);
/* 0～255 原始值：左杆上下、左杆左右、右杆左右。
 * 小于中心分别表示前进、左移、逆时针转；方向以车头为参照。
 * 出死区后只取方向，统一速度由 motor_set_speed_percent 设置。
 */
void motor(float joystick_forward, float joystick_sideways, float joystick_turn);
#endif
