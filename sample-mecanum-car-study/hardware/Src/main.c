#include "main.h"

#define CONTROL_LOOP_DELAY_MS  10U
static ps2_data ps2Data;
static uint8_t control_ready = 0;
static uint8_t button_was_down[PS2_BUTTON_COUNT] = {0};

/* Keil Watch 可观察；255 表示尚无松开事件。不会控制任何舵机。 */
volatile uint8_t last_released_button = 255U;

static void buttons_reset(void)
{
    uint8_t button;
    for (button = 0; button < PS2_BUTTON_COUNT; button++)
    {
        button_was_down[button] = 0;
    }
}

/* 用 switch 代替原函数指针数组：以后在相应 case 里写自己的动作。 */
static void on_button_released(uint8_t button)
{
    last_released_button = button;
    switch (button)
    {
        case PS2_BUTTON_CROSS:
            /* TODO_GRIPPER：例如夹爪闭合，目前不驱动舵机。 */
            break;
        case PS2_BUTTON_CIRCLE:
            /* TODO_GRIPPER：例如夹爪打开，目前不驱动舵机。 */
            break;
        default:
            /* 其余按键暂时只记录编号。动作不要长时间阻塞主循环。 */
            break;
    }
}

static void buttons_process(const ps2_data *data)
{
    uint8_t button;
    uint8_t is_down;
    for (button = 0; button < PS2_BUTTON_COUNT; button++)
    {
        /* 先选按键组，再提取对应的那一位。 */
        if (button < 8)
        {
            is_down = (uint8_t)((data->btn1 >> button) & 1U);
        }
        else
        {
            is_down = (uint8_t)((data->btn2 >> (button - 8)) & 1U);
        }
        /* 沿用原项目：从“按下”变成“松开”时，只执行一次。 */
        if (button_was_down[button] && !is_down)
        {
            on_button_released(button);
        }
        button_was_down[button] = is_down;
    }
}

static uint8_t controls_are_neutral(const ps2_data *data)
{
    return motor_joystick_is_centered(data->LJoy_UD) &&
           motor_joystick_is_centered(data->LJoy_LR) &&
           motor_joystick_is_centered(data->RJoy_LR) &&
           data->btn1 == 0 && data->btn2 == 0;
}

/* 单独写成一步，方便逐次调试，也能在电脑上测试异常/恢复的过程。 */
static void control_step(void)
{
    uint8_t frame_valid = ps2_read(&ps2Data);
    if (!frame_valid)
    {
        motor_stop();
        control_ready = 0;
        buttons_reset(); /* 丢包不能被当成“用户松开按钮”。 */
        return;
    }
    if (!control_ready)
    {
        motor_stop();
        buttons_reset();
        /* 上电/异常恢复后，先松开按键并让三个控制轴回中。 */
        if (controls_are_neutral(&ps2Data))
        {
            control_ready = 1;
        }
        return;
    }
    buttons_process(&ps2Data);
    /* uint8_t 会自动转换为函数需要的 float；这里不做速度换算。 */
    motor(ps2Data.LJoy_UD, ps2Data.LJoy_LR, ps2Data.RJoy_LR);
}

int main(void)
{
    delay_init();
    motor_init(); /* 初始化 TIM2/TIM3，八个输入从低电平启动 */
    motor_stop();
    ps2_init();
    while (1)
    {
        control_step();
        /* 通信和计算也耗时，循环总周期不是精确的 10 ms。 */
        delay_ms(CONTROL_LOOP_DELAY_MS);
    }
}
