#include "stm32f10x.h"
#include "delay.h"
#include "ps2.h"
#include "pwm.h"

/* 独立电机测试：×=1，○=2，□=3，△=4。
 * 每按一次翻转对应电机启停，按住不重复。四路相互独立。
 * PWM 为正向 100%；START 可以全部停止。
 * 实际电机转向由两根电机线和安装方向决定。
 */
#define MOTOR_TEST_DUTY 1.0f
#define MOTOR_TEST_DELAY_MS 10U

static const uint8_t motor_buttons[4] = {
    PS2_BUTTON_CROSS, PS2_BUTTON_CIRCLE,
    PS2_BUTTON_SQUARE, PS2_BUTTON_TRIANGLE
};

typedef struct
{
    uint32_t magic;          /* 0x4D42544E: motor buttons */
    uint32_t loops;
    uint32_t valid_frames;
    uint32_t invalid_frames;
    uint32_t armed;
    uint32_t motor_mask;     /* bit0..3=电机1..4，1运行、0停止 */
    uint32_t btn2;           /* 已取反：1按下 */
    uint32_t mode;
    uint32_t last_motor;     /* 最近切换的电机编号1..4；0表示尚无 */
    uint32_t toggles[4];
} motor_buttons_debug_t;

volatile motor_buttons_debug_t motor_buttons_debug;
static uint8_t previous_down;
static ps2_data controller;

static void motor_test_stop(void)
{
    pwm_stop_all();
    motor_buttons_debug.motor_mask = 0;
    motor_buttons_debug.armed = 0;
    previous_down = 0;
}

static void motor_test_step(void)
{
    uint8_t i;
    uint8_t down = 0;
    uint8_t pressed;
    uint8_t valid = ps2_read_buttons(&controller);
    motor_buttons_debug.loops++;
    motor_buttons_debug.mode = controller.mode;
    motor_buttons_debug.btn2 = controller.btn2;
    if (valid == 0U)
    {
        motor_buttons_debug.invalid_frames++;
        motor_test_stop();
        return;
    }
    motor_buttons_debug.valid_frames++;

    /* START 是附加的全部停止键；松开所有按键后重新允许测试。 */
    if ((controller.btn1 & (1U << PS2_BUTTON_START)) != 0U)
    {
        motor_test_stop();
        return;
    }
    if (motor_buttons_debug.armed == 0U)
    {
        pwm_stop_all();
        if (controller.btn1 == 0U && controller.btn2 == 0U)
            motor_buttons_debug.armed = 1;
        return;
    }

    for (i = 0; i < 4U; i++)
    {
        if ((controller.btn2 & (1U << (motor_buttons[i] - 8U))) != 0U)
            down = (uint8_t)(down | (1U << i));
    }
    pressed = (uint8_t)(down & (uint8_t)~previous_down);
    for (i = 0; i < 4U; i++)
    {
        if ((pressed & (1U << i)) != 0U)
        {
            motor_buttons_debug.motor_mask ^= (1U << i);
            motor_buttons_debug.toggles[i]++;
            motor_buttons_debug.last_motor = i + 1U;
        }
    }
    previous_down = down;

    /* 沿用仓库四对 DRV8833 输入；关闭状态写 0/0，属于滑行停止。 */
    pwm_set1((motor_buttons_debug.motor_mask & 1U) ? MOTOR_TEST_DUTY : 0.0f);
    pwm_set2((motor_buttons_debug.motor_mask & 2U) ? MOTOR_TEST_DUTY : 0.0f);
    pwm_set3((motor_buttons_debug.motor_mask & 4U) ? MOTOR_TEST_DUTY : 0.0f);
    pwm_set4((motor_buttons_debug.motor_mask & 8U) ? MOTOR_TEST_DUTY : 0.0f);
}

int main(void)
{
    delay_init();
    pwm_init();
    motor_test_stop();
    ps2_init();
    motor_buttons_debug.magic = 0x4D42544EU;
    while (1)
    {
        motor_test_step();
        delay_ms(MOTOR_TEST_DELAY_MS);
    }
}
