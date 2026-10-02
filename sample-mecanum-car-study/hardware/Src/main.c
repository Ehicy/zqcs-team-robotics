#include "main.h"
#include "stm32f10x.h"
#include "calibration_control.h"

#define CONTROL_LOOP_DELAY_MS  10U
#define SPEED_LED_PIN          GPIO_Pin_13
/* 每40次轮询切换一次，约0.5秒；不用阻塞延时影响手柄读取。 */
#define SPEED_LED_BLINK_STEPS  40U
static uint8_t speed_led_active = 0;
static uint8_t speed_led_on = 0;
static uint8_t speed_led_steps = 0;
static ps2_data ps2Data;
static uint8_t control_ready = 0;
static uint8_t button_was_down[PS2_BUTTON_COUNT] = {0};

/* Keil Watch 可观察；255 表示尚无松开事件。不会控制任何舵机。 */
volatile uint8_t last_released_button = 255U;
/* ST-Link/Watch：模式 0x41=数字，0x73=模拟，0=无效；循环数应增加。 */
volatile uint8_t control_mode = 0;
volatile uint32_t control_loops = 0;

static void speed_led_init(void)
{
    GPIO_InitTypeDef gpio;
    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOC, ENABLE);
    /* 核心板PC13用户灯低电平亮，先写高再设输出，上电默认灭。 */
    GPIO_SetBits(GPIOC, SPEED_LED_PIN);
    GPIO_StructInit(&gpio);
    gpio.GPIO_Pin = SPEED_LED_PIN;
    gpio.GPIO_Mode = GPIO_Mode_Out_PP;
    gpio.GPIO_Speed = GPIO_Speed_2MHz;
    GPIO_Init(GPIOC, &gpio);
    speed_led_active = 0;
    speed_led_on = 0;
    speed_led_steps = 0;
}

static void speed_led_step(void)
{
    uint8_t feedback_on;
    if (!motor_output_above_percent(0))
    {
        if (calibration_control_led(&feedback_on))
        {
            if (feedback_on) GPIO_ResetBits(GPIOC, SPEED_LED_PIN);
            else GPIO_SetBits(GPIOC, SPEED_LED_PIN);
            speed_led_active = speed_led_steps = speed_led_on = 0;
            return;
        }
    }
    else calibration_control_reset_feedback();
    if (!motor_output_above_percent(MOTOR_SPEED_LED_THRESHOLD))
    {
        GPIO_SetBits(GPIOC, SPEED_LED_PIN);
        speed_led_active = 0;
        speed_led_on = 0;
        speed_led_steps = 0;
        return;
    }
    /* 行驶时任一轮实际PWM严格超过60%才闪；停车时另有参数保存提示。
     * 不是电流、温度或通信报警。
     */
    if (!speed_led_active)
    {
        speed_led_active = 1;
        speed_led_on = 1;
        speed_led_steps = 0;
        GPIO_ResetBits(GPIOC, SPEED_LED_PIN);
        return;
    }
    if (++speed_led_steps >= SPEED_LED_BLINK_STEPS)
    {
        speed_led_steps = 0;
        speed_led_on = (uint8_t)!speed_led_on;
        if (speed_led_on) GPIO_ResetBits(GPIOC, SPEED_LED_PIN);
        else GPIO_SetBits(GPIOC, SPEED_LED_PIN);
    }
}

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
        /* 其他动作沿用原项目：从“按下”变成“松开”时只执行一次。 */
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
static void control_update(void)
{
    uint8_t frame_valid = ps2_read_buttons(&ps2Data);
    control_mode = ps2Data.mode;
    control_loops++;
    /* START在数字/模拟有效帧中都复位控制状态；优先于任何肩键。
     * 清空运动目标、输出/斜坡和按键历史；恢复默认上限100%。
     * 必须松键回中后再推杆才能启动；灯在本次step末随零输出立即灭。
     */
    if (frame_valid && (ps2Data.btn1 & (1U << PS2_BUTTON_START)) != 0U)
    {
        motor_set_speed_percent(MOTOR_SPEED_INITIAL_PERCENT);
        last_released_button = 255U;
        calibration_control_reset_feedback(); /* START撤驱动，保留用户已校准参数 */
    }
    /* 数字模式只用于诊断，摇杆控制必须有模拟轴数据。START 立即撤驱动。 */
    if (!frame_valid || ps2Data.mode != PS2_MODE_ANALOG ||
        (ps2Data.btn1 & (1U << PS2_BUTTON_START)) != 0U)
    {
        motor_stop();
        control_ready = 0;
        buttons_reset(); /* 丢包/停车不能被当成“用户松开按钮”。 */
        calibration_control_reset_inputs();
        return;
    }
    if (!control_ready)
    {
        motor_stop();
        buttons_reset();
        calibration_control_reset_inputs();
        /* 上电/异常恢复后，先松开按键并让三个控制轴回中。 */
        if (controls_are_neutral(&ps2Data))
        {
            control_ready = 1;
        }
        return;
    }
    if (calibration_control_poll(&ps2Data))
    {
        control_ready = 0; buttons_reset(); return; /* 保存后需松键回中 */
    }
    buttons_process(&ps2Data);
    /* uint8_t 会自动转换为函数需要的 float；这里不做速度换算。 */
    motor(ps2Data.LJoy_UD, ps2Data.LJoy_LR, ps2Data.RJoy_LR);
}

static void control_step(void)
{
    control_update();
    motor_update(); /* 无效帧/START已清空目标，不会在后续斜坡中重新启动 */
    /* 指示实际输出，松杆/异常/START撤输出后立即灭。 */
    speed_led_step();
}

int main(void)
{
    delay_init();
    motor_init(); /* 初始化 TIM2/TIM3，八个输入从低电平启动 */
    calibration_control_init(); /* 加载本版有效参数，未保存过则用100/95默认值 */
    motor_stop();
    speed_led_init();
    ps2_init();
    while (1)
    {
        control_step();
        /* 通信和计算也耗时，循环总周期不是精确的 10 ms。 */
        delay_ms(CONTROL_LOOP_DELAY_MS);
    }
}
