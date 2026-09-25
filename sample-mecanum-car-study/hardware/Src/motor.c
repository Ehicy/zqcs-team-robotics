#include "motor.h"
#include "pwm.h"
/* TIM2/TIM3 的八个 PWM 输入由 pwm 模块统一初始化。 */
void motor_init(void)
{
    pwm_init();
}

uint8_t motor_joystick_is_centered(uint8_t value)
{
    return value >= JOYSTICK_CENTER - JOYSTICK_DEADZONE &&
           value <= JOYSTICK_CENTER + JOYSTICK_DEADZONE;
}

/* 0～255 -> -1～1，沿用原方向：原始值小于中心时为正。
 * 回中区为 0；从死区边缘连续增长，端点 0/255 对应 +1/-1。
 */
static float motor_map(float value)
{
    float offset;
    if (!(value >= 0.0f && value <= 255.0f))
    {
        return 0.0f; /* 拒绝非法数值，包括 NaN。 */
    }
    offset = JOYSTICK_CENTER - value;
    if (offset > JOYSTICK_DEADZONE)
    {
        return (offset - JOYSTICK_DEADZONE) /
               (JOYSTICK_CENTER - JOYSTICK_DEADZONE);
    }
    if (offset < -JOYSTICK_DEADZONE)
    {
        return (offset + JOYSTICK_DEADZONE) /
               (255 - JOYSTICK_CENTER - JOYSTICK_DEADZONE);
    }
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
    float forward = motor_map(joystick_forward);
    float sideways = motor_map(joystick_sideways);
    float turn = motor_map(joystick_turn) * MOTOR_ROTATION_SCALE;
    float output[4];
    float largest = 1.0f;
    float magnitude;
    uint8_t wheel;

    /* 沿用原四轮组合和编号，实际轮位需架空确认。
     * 这是开环输出比例，不是编码器测得的轮速，没有 PID。
     */
    output[0] = (forward + sideways) / 2.0f - turn;
    output[1] = (forward - sideways) / 2.0f - turn;
    output[2] = (forward - sideways) / 2.0f + turn;
    output[3] = (forward + sideways) / 2.0f + turn;

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
        output[wheel] /= largest;
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
