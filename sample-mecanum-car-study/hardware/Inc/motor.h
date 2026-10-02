#ifndef MOTOR_H
#define MOTOR_H
#include <stdint.h>

/* 实测回中范围后再调整；默认 120～136（含端点）均视为回中。 */
#define JOYSTICK_CENTER       128
#define JOYSTICK_DEADZONE     8
#define MOTOR_SPEED_INITIAL_PERCENT 100U /* 输出上限；上电目标/实际输出均为0 */
#define MOTOR_SPEED_MAX_PERCENT     100U
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
/* 每次主循环调用：按实际毫秒时基推进 PWM 斜坡与换向等待。 */
void motor_update(void);
void motor_set_speed_percent(uint8_t percent);
uint8_t motor_get_speed_percent(void);
uint8_t motor_output_above_percent(uint8_t percent);
uint8_t motor_joystick_is_centered(uint8_t value);
/* 0～255 原始值：左杆上下、左杆左右、右杆左右。
 * 小于中心分别表示前进、左移、逆时针转；方向以车头为参照。
 * 出死区后幅度连续调PWM，motor_set_speed_percent 设置允许的输出上限。
 * 设置目标；非零输出由 motor_update 推进，零目标立即撤输出。
 */
void motor(float joystick_forward, float joystick_sideways, float joystick_turn);

typedef struct
{
    uint8_t forward_gain_percent;
    uint8_t reverse_gain_percent;
    uint8_t forward_min_percent;
    uint8_t reverse_min_percent;
} motor_calibration_t;
/* wheel=0..3：左后、右后、左前、右前；字段均 0..100，非法值拒绝。
 * 调参通常改 motor_tuning.h 默认表；此接口供运行中校准使用。
 */
uint8_t motor_set_calibration(uint8_t wheel, const motor_calibration_t *calibration);
#endif
