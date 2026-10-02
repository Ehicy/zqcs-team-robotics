#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "mock_hardware.h"

/* 将真正的主程序编入测试；不运行无限循环，逐次调用同一 control_step。 */
#define main firmware_main
#include "../hardware/Src/main.c"
#undef main

static void assert_stopped(void)
{
    unsigned i;
    for (i = 0; i < 8; i++) assert(mock_ccr[i] == 0);

}
static void run_frame(uint8_t forward, uint8_t sideways, uint8_t turn,
                      uint16_t pressed, uint8_t mode, uint8_t marker)
{
    uint8_t frame[9] = {0xFF, mode, marker, (uint8_t)~pressed,
                       (uint8_t)~(pressed >> 8), turn, 128, sideways, forward};
    mock_frame(frame, 9);
    control_step();
    mock_assert_poll_complete();
}
static void normal(uint8_t forward, uint8_t sideways, uint8_t turn, uint16_t pressed)
{ run_frame(forward, sideways, turn, pressed, 0x73, 0x5A); }

static void test_pwm(void)
{
    unsigned ch, i, other;
    void (*setters[4])(float) = {pwm_set1,pwm_set2,pwm_set3,pwm_set4};
    const float input[] = {0.0f, 0.00001f, 0.5f, 1.0f, 1.4f,
                           -0.5f, -1.0f, -1.4f, NAN, INFINITY, -INFINITY, -0.0f};
    const int expected[] = {0, 0, 1800, 3600, 3600, -1800, -3600, -3600,
                            0, 3600, -3600, 0};
    for (ch = 0; ch < 4; ch++)
    {
        for (i = 0; i < sizeof input / sizeof input[0]; i++)
        {
            setters[ch](input[i]);
            assert((int)mock_ccr[ch*2] - mock_ccr[ch*2+1] == expected[i]);
            for (other = 0; other < 8; other++)
                if (other / 2 != ch) assert(mock_ccr[other] == 0);
        }
        setters[ch](1.0f); setters[ch](-1.0f); setters[ch](1.0f);
        motor_stop(); assert_stopped();
    }
    puts("PASS: four DRV8833 pairs, signed PWM, saturation, NaN/Inf, reversal write order");
}

static void assert_wheels(int a, int b, int c, int d)
{
    const int expected[4] = {a,b,c,d};
    unsigned i;
    for (i = 0; i < 4; i++)
    {
        if (abs((int)mock_ccr[2*i] - mock_ccr[2*i+1] - expected[i]) > 1)
            fprintf(stderr, "wheel %u: actual=%d expected=%d speed=%u\n", i+1,
                    (int)mock_ccr[2*i] - mock_ccr[2*i+1], expected[i],
                    motor_get_speed_percent());
        assert(abs((int)mock_ccr[2*i] - mock_ccr[2*i+1] - expected[i]) <= 1);
    }
}

static void test_motor(void)
{
    unsigned i, j, k, axis;
    const uint8_t values[] = {0,1,64,119,120,127,128,129,136,137,192,254,255};
    assert(motor_get_speed_percent() == 0U);
    motor(0,128,128); assert_stopped();
    motor_set_speed_percent(10);
    motor(0,128,128); assert_wheels(-360,-360,360,360);
    motor_set_speed_percent(0); assert_stopped();
    motor(0,0,0); assert_stopped();
    motor_set_speed_percent(255); /* API 自身也封顶，不能绕过100%。 */
    assert(motor_get_speed_percent() == 100U);
    motor(0,128,128); assert_wheels(-3600,-3600,3600,3600);
    motor(128,128,0); assert_wheels(-3600,3600,3600,-3600);
    motor(0,0,0); assert_wheels(-1200,-1200,3600,-1200);
    motor_set_speed_percent(50);
    /* 按实测电气极性与轮位检查：1左后、2右后、3左前、4右前。
     * O 形左移：左前/右后前进，右前/左后后退。
     */
    motor(0,128,128); assert_wheels(-1800,-1800,1800,1800);
    motor(255,128,128); assert_wheels(1800,1800,-1800,-1800);
    motor(128,0,128); assert_wheels(1800,-1800,1800,-1800);
    motor(128,255,128); assert_wheels(-1800,1800,-1800,1800);
    motor(128,128,0); assert_wheels(-1800,1800,1800,-1800);
    motor(128,128,255); assert_wheels(1800,-1800,-1800,1800);
    /* 前左斜移只驱动左前和右后；前右斜移只驱动右前和左后。 */
    motor(0,0,128); assert_wheels(0,-1800,1800,0);
    motor(0,255,128); assert_wheels(-1800,0,0,1800);
    motor(255,0,128); assert_wheels(1800,0,0,-1800);
    motor(255,255,128); assert_wheels(0,1800,-1800,0);
    /* 平移与左转混合：电气输出 -1,-1,3,-1，统一除以 3 后乘 50%。 */
    motor(0,0,0); assert_wheels(-600,-600,1800,-600);
    /* 相同方向的小幅/大幅推杆必须同输出，右杆也只取方向。 */
    motor(119,128,128); assert_wheels(-1800,-1800,1800,1800);
    motor(137,128,128); assert_wheels(1800,1800,-1800,-1800);
    motor(128,119,128); assert_wheels(1800,-1800,1800,-1800);
    motor(128,128,119); assert_wheels(-1800,1800,1800,-1800);
    motor(0,64,128); assert_wheels(-600,-1800,1800,600);
    motor(64,96,128); assert_wheels(-600,-1800,1800,600);

    for (i = 0; i < 256; i++)
    {
        assert(motor_joystick_is_centered((uint8_t)i) == (i >= 120 && i <= 136));
        motor((float)i, 128, 128);
        if (i >= 120 && i <= 136) assert_stopped();
    }
    motor(119,128,128); assert(mock_ccr[1] > 0);
    motor(137,128,128); assert(mock_ccr[0] > 0);
    motor(NAN,128,128); assert_stopped();
    motor_set_speed_percent(100);
    for (i = 0; i < sizeof values; i++)
        for (j = 0; j < sizeof values; j++)
            for (k = 0; k < sizeof values; k++)
            {
                motor(values[i], values[j], values[k]);
                for (axis = 0; axis < 8; axis++) assert(mock_ccr[axis] <= 3600U);
            }
    motor_stop();
    puts("PASS: calibrated O wheels, direction only, 0/10/50/100% limit, 2197 mixed inputs");
}

static void test_ps2(void)
{
    ps2_data data;
    unsigned i;
    uint8_t frame[9] = {0xFF,0x73,0x5A,0xFE,0x7F,3,250,77,199};
    for (i = 0; i < 256; i++)
    {
        frame[5] = (uint8_t)i;
        mock_frame(frame, 9);
        assert(ps2_read(&data) == 1);
        mock_assert_poll_complete();
        assert(data.btn1 == 1 && data.btn2 == 128);
        assert(data.RJoy_LR == i && data.RJoy_UD == 250);
        assert(data.LJoy_LR == 77 && data.LJoy_UD == 199);
    }
    frame[2] = 0;
    mock_frame(frame,9);
    assert(ps2_read(&data) == 0);
    assert(data.mode == 0 && data.btn1 == 0 && data.btn2 == 0);
    assert(data.RJoy_LR == 128 && data.RJoy_UD == 128 &&
           data.LJoy_LR == 128 && data.LJoy_UD == 128);
    memset(frame,0xFF,9); mock_frame(frame,9); assert(ps2_read(&data) == 0);
    memset(frame,0,9); mock_frame(frame,9); assert(ps2_read(&data) == 0);
    assert(ps2_read(NULL) == 0);
    puts("PASS: LSB-first wire simulation, 256 byte values, frame layout, invalid clearing");
}

static void test_control(void)
{
    uint8_t button;
    const uint8_t modes[] = {0,0x41,0x79,0xFF};
    unsigned i;
    motor_set_speed_percent(MOTOR_SPEED_INITIAL_PERCENT);
    control_ready = 0;
    normal(0,128,128,0); assert_stopped(); /* 上电已推杆，不能启动 */
    normal(128,128,128,0); assert_stopped();
    normal(0,128,128,0); assert_stopped(); /* 初始速度0，回中使能后也不转 */
    assert(motor_get_speed_percent() == 0);
    normal(128,128,128,0x0800); assert_stopped(); /* R1加到10% */
    normal(128,128,128,0);
    normal(0,128,128,0); assert_wheels(-360,-360,360,360);
    assert(control_mode == PS2_MODE_ANALOG);

    for (i = 0; i < sizeof modes; i++)
    {
        run_frame(0,128,128,0,modes[i],0x5A); assert_stopped();
        normal(0,128,128,0); assert_stopped(); /* 连上但未回中 */
        normal(128,128,128,1); assert_stopped(); /* 按键未松 */
        normal(128,128,128,0); assert_stopped();
        normal(0,128,128,0); assert_wheels(-360,-360,360,360);
    }
    run_frame(0,128,128,0,0x73,0); assert_stopped();
    normal(128,128,128,0); assert_stopped();

    /* START 优先于推杆和普通按键；松开 START 后仍须回中。 */
    normal(0,128,128,0); assert_wheels(-360,-360,360,360);
    normal(0,0,0,(uint16_t)(1U << PS2_BUTTON_START)); assert_stopped();
    assert(control_ready == 0);
    assert(motor_get_speed_percent() == 0);
    assert(last_released_button == 255);
    assert(mock_gpio_c & GPIO_Pin_13);
    normal(0,128,128,0); assert_stopped();
    normal(128,128,128,(uint16_t)(1U << PS2_BUTTON_START)); assert_stopped();
    normal(128,128,128,0); assert_stopped();
    assert(control_ready == 1);
    normal(0,128,128,0); assert_stopped(); /* START后必须重新加速 */
    normal(128,128,128,0x0800);
    normal(128,128,128,0);
    normal(128,0,128,0); assert_wheels(360,-360,360,-360);
    normal(128,128,0,0); assert_wheels(-360,360,360,-360);
    normal(128,128,128,0); assert_stopped();

    /* 其余 15 个按键均应在松开时触发一次，按住/持续松开不重复。 */
    for (button = 0; button < PS2_BUTTON_COUNT; button++)
    {
        if (button == PS2_BUTTON_START) continue;
        last_released_button = 255;
        normal(128,128,128,(uint16_t)(1U << button));
        normal(128,128,128,(uint16_t)(1U << button));
        assert(last_released_button == 255);
        normal(128,128,128,0); assert(last_released_button == button);
        last_released_button = 255;
        normal(128,128,128,0); assert(last_released_button == 255);
    }
    /* 按住时丢帧，不能伪造松开事件；恢复后也不能触发旧动作。 */
    normal(0,128,128,(uint16_t)(1U << PS2_BUTTON_CROSS));
    last_released_button = 255;
    run_frame(0,128,128,0,0xFF,0xFF);
    assert_stopped(); assert(last_released_button == 255);
    normal(128,128,128,0);
    normal(128,128,128,0);
    assert(last_released_button == 255);
    puts("PASS: zero-speed startup, analog-only, START reset/rearm, 15 buttons, no phantom release");
}

static void test_speed_buttons(void)
{
    unsigned i;
    /* 独立用协议字节掩码：L1=0400，R1=0800；不靠被测枚举生成期望。 */
    motor_set_speed_percent(0);
    control_ready = 0;
    normal(128,128,128,0x0800); assert_stopped();
    assert(motor_get_speed_percent() == 0);
    normal(128,128,128,0); assert_stopped();
    normal(119,128,128,0x0800); assert_wheels(-360,-360,360,360);
    for (i = 0; i < 20; i++) normal(0,128,128,0x0800);
    assert(motor_get_speed_percent() == 10); /* 长按不连加，幅度不控速 */
    normal(0,128,128,0); assert_wheels(-360,-360,360,360);
    for (i = 0; i < 15; i++)
    {
        normal(0,128,128,0x0800);
        normal(0,128,128,0);
    }
    assert(motor_get_speed_percent() == 100);
    normal(0,0,0,0); assert_wheels(-1200,-1200,3600,-1200);
    normal(128,128,0,0); assert_wheels(-3600,3600,3600,-3600);
    normal(0,128,128,0x0300); /* L2/R2 不调速度 */
    assert(motor_get_speed_percent() == 100);
    normal(0,128,128,0);
    normal(0,128,128,0x0C00); /* L1/R1 同按不调速度 */
    assert(motor_get_speed_percent() == 100);
    normal(0,128,128,0);
    normal(0,128,128,0x0400); assert_wheels(-3240,-3240,3240,3240);
    for (i = 0; i < 20; i++) normal(0,128,128,0x0400);
    assert(motor_get_speed_percent() == 90);
    normal(0,128,128,0);
    for (i = 0; i < 12; i++)
    {
        normal(0,128,128,0x0400);
        normal(0,128,128,0);
    }
    assert(motor_get_speed_percent() == 0); assert_stopped();
    normal(0,128,128,0x0800); assert_wheels(-360,-360,360,360);
    run_frame(0,128,128,0x0800,0x41,0x5A); assert_stopped();
    assert(control_mode == 0x41);
    normal(128,128,128,0x0800); assert_stopped();
    assert(motor_get_speed_percent() == 10); /* 恢复时按住不能误加速 */
    normal(128,128,128,0); assert_stopped();
    normal(0,128,128,0x0800); assert_wheels(-720,-720,720,720);
    normal(128,128,128,0); assert_stopped();
    motor_init(); assert(motor_get_speed_percent() == 0);
    puts("PASS: wire L1/R1, initial 0%, 10% steps, held/simultaneous, 0..100%, rearm");
}

static void test_speed_led(void)
{
    unsigned i;
    motor_set_speed_percent(0);
    control_ready = 0;
    normal(128,128,128,0);
    assert(mock_gpio_c & GPIO_Pin_13); /* 初始0%灭 */
    for (i = 0; i < 6; i++)
    {
        normal(128,128,128,0x0800);
        normal(128,128,128,0);
        assert(mock_gpio_c & GPIO_Pin_13); /* 10..60%均灭 */
    }
    assert(motor_get_speed_percent() == 60);
    normal(128,128,128,0x0800); /* 实际R1帧到70%，立刻亮 */
    assert(motor_get_speed_percent() == 70);
    assert(!(mock_gpio_c & GPIO_Pin_13));
    for (i = 0; i < SPEED_LED_BLINK_STEPS; i++) normal(128,128,128,0);
    assert(mock_gpio_c & GPIO_Pin_13);
    for (i = 0; i < SPEED_LED_BLINK_STEPS; i++) normal(128,128,128,0);
    assert(!(mock_gpio_c & GPIO_Pin_13));
    normal(128,128,128,0x0400); /* L1回60%，立即灭并清闪烁相位 */
    assert(motor_get_speed_percent() == 60);
    assert(mock_gpio_c & GPIO_Pin_13);
    normal(128,128,128,0);
    normal(128,128,128,0x0800); /* 重新到70% */
    assert(motor_get_speed_percent() == 70);
    assert(!(mock_gpio_c & GPIO_Pin_13));
    for (i = 0; i < SPEED_LED_BLINK_STEPS; i++)
        run_frame(128,128,128,0,0x41,0x5A);
    assert(mock_gpio_c & GPIO_Pin_13); assert_stopped();
    /* START在数字模式也重置，且优先于同时按下的R1。 */
    last_released_button = 11;
    run_frame(0,0,0,0x0808,0x41,0x5A);
    assert(motor_get_speed_percent() == 0); assert_stopped();
    assert(control_ready == 0 && last_released_button == 255);
    assert(mock_gpio_c & GPIO_Pin_13);
    assert(speed_led_active == 0 && speed_led_steps == 0 && speed_led_on == 0);
    for (i = 0; i < PS2_BUTTON_COUNT; i++) assert(button_was_down[i] == 0);
    normal(128,128,128,0x0800); assert_stopped(); /* R1未松，不能恢复 */
    assert(motor_get_speed_percent() == 0 && control_ready == 0);
    normal(128,128,128,0); assert_stopped();
    normal(0,128,128,0); assert_stopped(); /* 恢复后0%仍停 */
    run_frame(128,128,128,0,0xFF,0xFF);
    assert(mock_gpio_c & GPIO_Pin_13);
    puts("PASS: PC13 off at 0..60%, blink at 70%, START resets speed/buttons/LED in digital mode");
}

int main(void)
{
    motor_init(); /* 同时初始化两个定时器和八路输出 */
    assert(mock_af_a == (GPIO_Pin_0 | GPIO_Pin_1 | GPIO_Pin_2 | GPIO_Pin_3 |
                         GPIO_Pin_6 | GPIO_Pin_7));
    assert(mock_af_b == (GPIO_Pin_0 | GPIO_Pin_1));
    ps2_init();
    speed_led_init();
    assert(mock_period[0] == 3599 && mock_prescaler[0] == 0);
    assert(mock_period[1] == 3599 && mock_prescaler[1] == 0);
    assert_stopped();
    test_pwm();
    test_motor();
    test_ps2();
    test_control();
    test_speed_buttons();
    test_speed_led();
    puts("All host tests passed (not a hardware or Keil build test).");
    return 0;
}
