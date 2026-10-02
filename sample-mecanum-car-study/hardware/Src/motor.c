#include "motor.h"
#include "pwm.h"
/* volatile 便于 ST-Link/Watch 查看；所有写入都经过 100% 上限函数。 */
static volatile uint8_t speed_percent = MOTOR_SPEED_INITIAL_PERCENT;
/* TIM2/TIM3 的八个 PWM 输入由 pwm 模块统一初始化。 */
void motor_init(void)
{
    pwm_init();
    motor_set_speed_percent(MOTOR_SPEED_INITIAL_PERCENT);
}

void motor_set_speed_percent(uint8_t percent)
{
    speed_percent = percent > MOTOR_SPEED_MAX_PERCENT ?
                    MOTOR_SPEED_MAX_PERCENT : percent;
    if (speed_percent == 0U) motor_stop();
}

uint8_t motor_get_speed_percent(void)
{
    return speed_percent;
}

uint8_t motor_joystick_is_centered(uint8_t value)
{
    return value >= JOYSTICK_CENTER - JOYSTICK_DEADZONE &&
           value <= JOYSTICK_CENTER + JOYSTICK_DEADZONE;
}

/* 只取离中心的方向向量：稍后归一化，推杆幅度不再决定速度。
 * 每轴回中区 120～136 置零，原始值小于中心时为正。
 */
static float motor_axis_offset(float value)
{
    float offset;
    if (!(value >= 0.0f && value <= 255.0f))
    {
        return 0.0f; /* 拒绝非法数值，包括 NaN。 */
    }
    offset = JOYSTICK_CENTER - value;
    /* 中心 128 两侧分别有 128/127 个计数，端点校正后对称。 */
    if (offset > JOYSTICK_DEADZONE)
        return offset / JOYSTICK_CENTER;
    if (offset < -JOYSTICK_DEADZONE)
        return offset / (255 - JOYSTICK_CENTER);
    return 0.0f;
}

static float absolute_value(float value)
{
    return value < 0.0f ? -value : value;
}

void motor_stop(void)
{
    /* DRV8833 双输入均为 0：滑行，不是主动制动。 */
    pwm_stop_all();
}

void motor(float joystick_forward, float joystick_sideways, float joystick_turn)
{
    float forward = motor_axis_offset(joystick_forward);
    float sideways = motor_axis_offset(joystick_sideways);
    float turn = motor_axis_offset(joystick_turn);
    float direction_scale = absolute_value(forward);
    float speed = (float)motor_get_speed_percent() / 100.0f;
    /* 2026-09-30 实测：正电气输出使两只后轮后退、两只前轮前进。
     * 顺序：1左后(×)、2右后(○)、3左前(□)、4右前(△)。
     */
    static const float polarity[4] = {-1.0f, -1.0f, 1.0f, 1.0f};
    float output[4];
    float largest = 1.0f;
    float magnitude;
    uint8_t wheel;

    /* 左杆保留平移方向/分量比例，消除推杆幅度；右杆只取左/右。 */
    if (absolute_value(sideways) > direction_scale)
        direction_scale = absolute_value(sideways);
    if (direction_scale > 0.0f)
    {
        forward /= direction_scale;
        sideways /= direction_scale;
    }
    turn = (turn > 0.0f ? 1.0f : (turn < 0.0f ? -1.0f : 0.0f)) *
           MOTOR_O_ROTATION_SIGN;

    /* 俯视 O 形滚子，F=前、L=左；先算“让车前进”为正的轮输出。
     * O 形的旋转杠杆为半轴距减半轮距，不能套 X 形的加和。
     * 默认前后轴距 > 左右轮距，turn 是旋转轮输出的方向分量。
     * 这是开环 PWM 比例，不是编码器测得的轮速，没有 PID。
     */
    output[0] = forward - sideways + turn; /* 左后 */
    output[1] = forward + sideways - turn; /* 右后 */
    output[2] = forward + sideways + turn; /* 左前 */
    output[3] = forward - sideways - turn; /* 右前 */

    /* 超过 1 时四轮一起缩小，保留四轮之间的比例。 */
    for (wheel = 0; wheel < 4; wheel++)
    {
        magnitude = absolute_value(output[wheel]);
        if (magnitude > largest)
        {
            largest = magnitude;
        }
    }
    for (wheel = 0; wheel < 4; wheel++)
    {
        /* 先整体限幅，再乘统一速度；任意混合均不超过选定百分比。 */
        output[wheel] = output[wheel] / largest * speed * polarity[wheel];
    }

    /* 带符号输出：正向 PWM/0，反向 0/PWM，零值 0/0。
     * PWM 层先清除反方向输入，再写入当前方向，避免出现双高。
     * 这不是机械减速斜坡；快速反转的电流仍须实车验证。
     */
    pwm_set1(output[0]);
    pwm_set2(output[1]);
    pwm_set3(output[2]);
    pwm_set4(output[3]);
}
